use serde::{Deserialize, Serialize};
use std::rc::Rc;

/// 箱子的摆放方向
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
pub enum Orient {
    /// 长度(lx)沿X轴，宽度(ly)沿Y轴，高度(lz)沿Z轴
    #[default]
    XYZ,
    /// 宽度(ly)沿X轴，长度(lx)沿Y轴，高度(lz)沿Z轴
    YXZ,
    /// 长度(lx)沿X轴，高度(lz)沿Y轴，宽度(ly)沿Z轴
    XZY,
    /// 高度(lz)沿X轴，长度(lx)沿Y轴，宽度(ly)沿Z轴
    ZXY,
    /// 宽度(ly)沿X轴，高度(lz)沿Y轴，长度(lx)沿Z轴
    YZX,
    /// 高度(lz)沿X轴，宽度(ly)沿Y轴，长度(lx)沿Z轴
    ZYX,
}

/// 箱型定义
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct BoxType {
    /// 箱型ID
    pub id: String,
    /// 箱型长度
    pub lx: i32,
    /// 箱型宽度
    pub ly: i32,
    /// 箱型高度
    pub lz: i32,
    /// 箱型允许的摆放方向，默认只能平放
    #[serde(default = "default_orients")]
    pub orients: Vec<Orient>,
}

fn default_orients() -> Vec<Orient> {
    vec![Orient::XYZ, Orient::YXZ]
}

impl BoxType {
    /// 计算箱型体积
    pub fn volume(&self) -> i64 {
        self.lx as i64 * self.ly as i64 * self.lz as i64
    }
}

impl PartialEq for BoxType {
    fn eq(&self, other: &Self) -> bool {
        self.id == other.id
    }
}

/// 箱子定义
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Box {
    /// 箱子ID
    pub id: String,
    /// 箱型ID
    #[serde(rename = "type")]
    pub type_id: String,
    /// 箱子重量
    #[serde(skip_serializing)]
    pub weight: Option<f64>,

    /// 箱子位置
    #[serde(skip_serializing_if = "Option::is_none")]
    pub x: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub y: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub z: Option<i32>,
    /// 箱子摆放方向
    #[serde(default = "Orient::default")]
    pub orient: Orient,

    /// 当前方向下的尺寸（内部属性）
    #[serde(skip)]
    pub lx: i32,
    #[serde(skip)]
    pub ly: i32,
    #[serde(skip)]
    pub lz: i32,
    /// 箱子类型指针（内部属性）
    #[serde(skip)]
    pub box_type: Rc<BoxType>,
}

impl Box {
    /// 计算箱子体积
    pub fn volume(&self) -> i64 {
        self.box_type.volume()
    }

    /// 设置箱子摆放方向
    pub fn set_orient(&mut self, orient: Orient) {
        self.orient = orient;
        match orient {
            Orient::XYZ => {
                self.lx = self.box_type.lx;
                self.ly = self.box_type.ly;
                self.lz = self.box_type.lz;
            }
            Orient::YXZ => {
                self.lx = self.box_type.ly;
                self.ly = self.box_type.lx;
                self.lz = self.box_type.lz;
            }
            Orient::XZY => {
                self.lx = self.box_type.lx;
                self.ly = self.box_type.lz;
                self.lz = self.box_type.ly;
            }
            Orient::ZXY => {
                self.lx = self.box_type.lz;
                self.ly = self.box_type.lx;
                self.lz = self.box_type.ly;
            }
            Orient::YZX => {
                self.lx = self.box_type.ly;
                self.ly = self.box_type.lz;
                self.lz = self.box_type.lx;
            }
            Orient::ZYX => {
                self.lx = self.box_type.lz;
                self.ly = self.box_type.ly;
                self.lz = self.box_type.lx;
            }
        }
    }
}

impl PartialEq for Box {
    fn eq(&self, other: &Self) -> bool {
        self.id == other.id
    }
}

/// 容器类型定义
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ContainerType {
    /// 容器类型ID
    pub id: String,
    /// 容器类型长度
    pub lx: i32,
    /// 容器类型宽度
    pub ly: i32,
    /// 容器类型高度
    pub lz: i32,
    /// 容器类型载重
    #[serde(skip_serializing)]
    pub payload: Option<f64>,
    /// 容器类型数量（默认无限制）
    #[serde(skip_serializing)]
    pub quantity: Option<i32>,
}

impl ContainerType {
    /// 计算容器体积
    pub fn volume(&self) -> i64 {
        self.lx as i64 * self.ly as i64 * self.lz as i64
    }
}

/// 容器定义
#[derive(Debug, Clone, Serialize, Default)]
pub struct Container {
    /// 容器类型
    #[serde(rename = "type")]
    pub container_type: ContainerType,
    /// 装载的箱子
    pub boxes: Vec<Box>,
    /// 体积利用率
    pub volume_rate: f64,
    /// 重量利用率
    #[serde(skip_serializing_if = "Option::is_none")]
    pub weight_rate: Option<f64>,
}

/// 输入数据结构
#[derive(Debug, Clone, Deserialize)]
pub struct Input {
    /// 箱型列表
    pub box_types: Vec<BoxType>,
    /// 容器类型列表
    pub container_types: Vec<ContainerType>,
    /// 箱子列表
    pub boxes: Vec<Box>,
    /// 箱子最小支撑比例，默认0.7
    #[serde(default = "default_support_rate")]
    pub support_rate: f64,
}

fn default_support_rate() -> f64 {
    0.7
}

/// 输出数据结构
#[derive(Debug, Clone, Serialize)]
pub struct Output {
    /// 箱型列表
    pub box_types: Vec<BoxType>,
    /// 容器列表
    pub containers: Vec<Container>,
    /// 未装载的箱子
    pub unpacked_boxes: Vec<Box>,
}
