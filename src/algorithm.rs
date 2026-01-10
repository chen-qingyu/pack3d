use crate::constraint::Constraint;
use crate::entities::{Box, Container, ContainerType, Input, Output};
use log::{info, warn};

/// 装箱算法
pub struct Algorithm {
    /// 容器类型列表
    container_types: Vec<ContainerType>,
    /// 当前容器
    container: Container,
    /// 待装载箱子
    unpacked_boxes: Vec<Box>,
    /// 已装载箱子
    packed_boxes: Vec<Box>,
    /// 装箱结果
    output: Output,
    /// 箱子最小支撑比例
    support_rate: f64,
}

impl Algorithm {
    /// 创建新的装箱算法实例
    pub fn new(input: Input) -> Self {
        let mut unpacked_boxes = input.boxes;
        let mut container_types = input.container_types;

        // 箱子按体积从大到小排序
        unpacked_boxes.sort_by_key(|a| std::cmp::Reverse(a.volume()));

        // 容器类型按体积从小到大排序
        container_types.sort_by_key(|a| a.volume());

        // 创建空容器
        let container = Container::default();

        Algorithm {
            container_types,
            container,
            unpacked_boxes,
            packed_boxes: Vec::new(),
            output: Output {
                box_types: input.box_types,
                containers: Vec::new(),
                unpacked_boxes: Vec::new(),
            },
            support_rate: input.support_rate,
        }
    }

    /// 执行装箱算法
    pub fn run(mut self) -> Output {
        // 逐个容器装箱，直到容器用完或箱子装完
        while !self.unpacked_boxes.is_empty() {
            // 生成下一个可用容器
            if !self.generate_container() {
                break; // 没有可用容器了
            }

            // 清空已装载的箱子
            self.packed_boxes.clear();

            // 尝试装载箱子
            let boxes_to_try = self.unpacked_boxes.clone();
            for mut item in boxes_to_try {
                if self.try_pack(&mut item) {
                    self.packed_boxes.push(item);
                }
            }

            // 如果一个箱子都装不下，说明剩余箱子无法装载，退出循环
            if self.packed_boxes.is_empty() {
                warn!("Remaining boxes cannot be packed.");
                break;
            }

            // 更新剩余箱子列表
            self.unpacked_boxes
                .retain(|b| !self.packed_boxes.iter().any(|pb| pb.id == b.id));

            // 创建装箱方案
            self.container.boxes = self.packed_boxes.clone();
            self.container.volume_rate = self.calculate_volume_rate();
            self.container.weight_rate = self.calculate_weight_rate();
            self.output.containers.push(self.container.clone());

            info!(
                "Container \"{}\": packed {}, left {}, volume rate: {:.2}%.",
                self.container.container_type.id,
                self.packed_boxes.len(),
                self.unpacked_boxes.len(),
                self.container.volume_rate * 100.0
            );
        }

        if !self.unpacked_boxes.is_empty() {
            warn!("Unpacked {} boxes.", self.unpacked_boxes.len());
        }

        self.output.unpacked_boxes = self.unpacked_boxes;
        self.output
    }

    /// 尝试装载箱子
    fn try_pack(&self, item: &mut Box) -> bool {
        // 包括原点（左前角）在内的所有可能的装载位置（候选点）
        let mut candidates = vec![(0, 0, 0)];

        // 在已装载箱子的右方、后方、上方生成候选点
        for b in &self.packed_boxes {
            let x = b.x.unwrap();
            let y = b.y.unwrap();
            let z = b.z.unwrap();
            candidates.push((x + b.lx, y, z)); // 右方
            candidates.push((x, y + b.ly, z)); // 后方
            candidates.push((x, y, z + b.lz)); // 上方
        }

        // 候选点排序，优先级：z（高度/上方）> y（宽度/后方）> x（长度/右方）
        candidates.sort();
        // 候选点去重
        candidates.dedup();

        // 创建约束检查器
        let constraint = Constraint::new(&self.container, &self.packed_boxes, self.support_rate);

        // 尝试每个允许的方向
        for orient in item.box_type.orients.clone() {
            item.set_orient(orient);

            // 尝试每个候选位置
            for &(x, y, z) in &candidates {
                item.x = Some(x);
                item.y = Some(y);
                item.z = Some(z);

                // 检查所有约束
                if constraint.check_constraints(item) {
                    return true; // 找到有效位置
                }
            }
        }

        false // 没有找到有效位置
    }

    /// 计算体积利用率
    fn calculate_volume_rate(&self) -> f64 {
        let used_volume: i64 = self.packed_boxes.iter().map(|b| b.volume()).sum();
        used_volume as f64 / self.container.container_type.volume() as f64
    }

    /// 计算重量利用率
    fn calculate_weight_rate(&self) -> Option<f64> {
        self.container.container_type.payload?; // 容器未设置载重

        let used_weight: f64 = self.packed_boxes.iter().map(|b| b.weight.unwrap()).sum();
        Some(used_weight / self.container.container_type.payload.unwrap())
    }

    /// 生成可用容器
    fn generate_container(&mut self) -> bool {
        // 计算剩余箱子总体积
        let unpacked_volume: i64 = self.unpacked_boxes.iter().map(|b| b.volume()).sum();

        // 按体积从小到大查找合适的容器类型
        for container_type in &mut self.container_types {
            if container_type.quantity.is_none_or(|q| q > 0)
                && container_type.volume() >= unpacked_volume
            {
                container_type.quantity = container_type.quantity.map(|q| q - 1);
                self.container.container_type = container_type.clone();
                return true;
            }
        }

        // 否则使用最大的可用容器类型
        for container_type in self.container_types.iter_mut().rev() {
            if container_type.quantity.is_none_or(|q| q > 0) {
                container_type.quantity = container_type.quantity.map(|q| q - 1);
                self.container.container_type = container_type.clone();
                return true;
            }
        }

        false // 所有容器都用完了
    }
}
