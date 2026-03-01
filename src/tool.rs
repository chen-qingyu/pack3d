use crate::entities::Input;
use log::{error, info};
use std::collections::HashSet;
use std::fs;
use std::process;
use std::rc::Rc;

/// 校验JSON输入数据格式
fn validate_schema(input_json: &serde_json::Value) {
    let schema = include_str!("../data/input_schema.json");
    let schema = serde_json::from_str(schema).unwrap();
    if let Err(error) = jsonschema::validate(&schema, input_json) {
        error!("JSON schema validation failed: {}", error);
        process::exit(1);
    };
}

/// 校验输入数据的逻辑关系
fn validate_logic(input: &Input) {
    // 规则1：容器类型id全局唯一
    let mut container_type_ids = HashSet::new();
    for ct in &input.container_types {
        if !container_type_ids.insert(&ct.id) {
            error!("Duplicate container type id: {}.", ct.id);
            process::exit(1);
        }
    }

    // 规则2：箱子id全局唯一
    let mut box_ids = HashSet::new();
    for b in &input.boxes {
        if !box_ids.insert(&b.id) {
            error!("Duplicate box id: {}.", b.id);
            process::exit(1);
        }
    }

    // 规则3：箱型id全局唯一
    let mut box_type_ids = HashSet::new();
    for bt in &input.box_types {
        if !box_type_ids.insert(&bt.id) {
            error!("Duplicate box type id: {}.", bt.id);
            process::exit(1);
        }
    }

    // 规则4：箱子引用的箱型必须存在
    for b in &input.boxes {
        if !box_type_ids.contains(&b.type_id) {
            error!(
                "Box \"{}\" references non-existent box type \"{}\".",
                b.id, b.type_id
            );
            process::exit(1);
        }
    }

    // 规则5：当容器有载重限制时，箱子必须有重量信息
    let has_payload_limit = input.container_types.iter().any(|ct| ct.payload.is_some());
    if has_payload_limit {
        for b in &input.boxes {
            if b.weight.is_none() {
                error!(
                    "Box \"{}\" has no weight info but container has payload limit.",
                    b.id
                );
                process::exit(1);
            }
        }
    }

    // 规则6：至少要有一个容器类型和一个箱子
    if input.container_types.is_empty() {
        error!("No container types provided.");
        process::exit(1);
    }
    if input.boxes.is_empty() {
        error!("No boxes provided.");
        process::exit(1);
    }
}

/// 解析JSON输入文件并校验
pub fn parse_input(input_file: &str) -> Input {
    // 读取文件
    let content = fs::read_to_string(input_file).unwrap_or_else(|err| {
        error!("Failed to open input file \"{}\": {}.", input_file, err);
        process::exit(1);
    });
    let input_json = serde_json::from_str(&content).unwrap_or_else(|err| {
        error!(
            "Failed to parse input file \"{}\" as JSON: {}.",
            input_file, err
        );
        process::exit(1);
    });

    // 校验输入
    validate_schema(&input_json);
    let mut input: Input = serde_json::from_value(input_json).unwrap();

    // 关联box与box_type
    let box_type_map: std::collections::HashMap<&str, Rc<_>> = input
        .box_types
        .iter()
        .map(|bt| (bt.id.as_str(), Rc::new(bt.clone())))
        .collect();
    for item in &mut input.boxes {
        let box_type = box_type_map[item.type_id.as_str()].clone();
        item.lx = box_type.lx;
        item.ly = box_type.ly;
        item.lz = box_type.lz;
        item.box_type = box_type;
    }

    validate_logic(&input);
    info!("Successfully validated input \"{}\".", input_file);

    input
}
