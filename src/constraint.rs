use crate::entities::{Box, Container};

/// 约束检查器
pub struct Constraint<'a> {
    /// 当前容器的引用
    container: &'a Container,
    /// 已装载箱子的引用
    packed_boxes: &'a [Box],
    /// 箱子最小支撑比例
    support_rate: f64,
}

impl<'a> Constraint<'a> {
    /// 创建新的约束检查器
    pub fn new(container: &'a Container, packed_boxes: &'a [Box], support_rate: f64) -> Self {
        Constraint {
            container,
            packed_boxes,
            support_rate,
        }
    }

    /// 约束1：箱子不能超出容器边界
    pub fn check_bound(&self, item: &Box) -> bool {
        let x = item.x.unwrap();
        let y = item.y.unwrap();
        let z = item.z.unwrap();

        x + item.lx <= self.container.container_type.lx
            && y + item.ly <= self.container.container_type.ly
            && z + item.lz <= self.container.container_type.lz
    }

    /// 约束2：箱子不能与已装载的箱子重叠
    pub fn check_overlap(&self, item: &Box) -> bool {
        let x = item.x.unwrap();
        let y = item.y.unwrap();
        let z = item.z.unwrap();

        for packed_box in self.packed_boxes {
            let px = packed_box.x.unwrap();
            let py = packed_box.y.unwrap();
            let pz = packed_box.z.unwrap();

            let x_overlap = x < px + packed_box.lx && px < x + item.lx;
            let y_overlap = y < py + packed_box.ly && py < y + item.ly;
            let z_overlap = z < pz + packed_box.lz && pz < z + item.lz;

            // 三个方向都重叠才是重叠
            if x_overlap && y_overlap && z_overlap {
                return false;
            }
        }
        true
    }

    /// 约束3：箱子不能缺乏足够的支撑
    pub fn check_support(&self, item: &Box) -> bool {
        let x = item.x.unwrap();
        let y = item.y.unwrap();
        let z = item.z.unwrap();

        // 如果箱子直接放在容器底面上，或者支撑比例为0，则认为有支撑
        if z == 0 || self.support_rate == 0.0 {
            return true;
        }

        // 检查所有已装载的箱子
        let mut support_area = 0;
        for support_box in self.packed_boxes {
            let sx = support_box.x.unwrap();
            let sy = support_box.y.unwrap();
            let sz = support_box.z.unwrap();

            // 只考虑正好在当前箱子下方平面的箱子
            if sz + support_box.lz != z {
                continue;
            }

            // 计算重叠的矩形区域
            let overlap_x1 = i32::max(x, sx);
            let overlap_y1 = i32::max(y, sy);
            let overlap_x2 = i32::min(x + item.lx, sx + support_box.lx);
            let overlap_y2 = i32::min(y + item.ly, sy + support_box.ly);

            if overlap_x1 < overlap_x2 && overlap_y1 < overlap_y2 {
                support_area += (overlap_x2 - overlap_x1) * (overlap_y2 - overlap_y1);
            }
        }

        // 要求底面被支撑的比例达到设定的最小支撑比例
        (support_area as f64) >= (item.lx * item.ly) as f64 * self.support_rate
    }

    /// 约束4：箱子总重量不能超过容器载重（若输入存在载重限制）
    pub fn check_weight(&self, item: &Box) -> bool {
        if let Some(payload) = self.container.container_type.payload {
            let mut total: f64 = self.packed_boxes.iter().map(|b| b.weight.unwrap()).sum();
            total += item.weight.unwrap();
            total <= payload
        } else {
            true
        }
    }

    /// 检查是否满足所有约束
    pub fn check_constraints(&self, item: &Box) -> bool {
        self.check_bound(item)
            && self.check_overlap(item)
            && self.check_support(item)
            && self.check_weight(item)
    }
}
