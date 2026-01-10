use pack3d::algorithm::Algorithm;
use pack3d::constraint::Constraint;
use pack3d::tool::parse_input;
use std::collections::HashSet;
use std::fs;

#[test]
fn test_output() {
    // 遍历所有测试文件
    for entry in fs::read_dir("data/tests/").unwrap() {
        let input_path = entry.unwrap().path();

        // 运行算法并输出结果
        let input = parse_input(input_path.to_str().unwrap());
        let output = Algorithm::new(input.clone()).run();

        // 1. 确保输入箱子与输出箱子数量相等
        let mut out_box_cnt: usize = output.containers.iter().map(|c| c.boxes.len()).sum();
        out_box_cnt += output.unpacked_boxes.len();
        assert_eq!(input.boxes.len(), out_box_cnt);

        // 2. 确保输入箱子与输出箱子实体相等
        let input_box_ids: HashSet<_> = input.boxes.iter().map(|b| &b.id).collect();
        let mut output_box_ids: HashSet<_> = output.unpacked_boxes.iter().map(|b| &b.id).collect();
        for container in &output.containers {
            output_box_ids.extend(container.boxes.iter().map(|b| &b.id));
        }
        assert_eq!(input_box_ids, output_box_ids);

        // 3. 确保输出的每个已装载箱子都符合约束
        for container in &output.containers {
            for (i, item) in container.boxes.iter().enumerate() {
                let mut boxes = container.boxes.clone();
                boxes.remove(i);
                let constraint = Constraint::new(container, &boxes, 0.7);
                assert!(constraint.check_constraints(item));
            }
        }

        // 4. 校验输出数据JSON格式
        let schema = serde_json::from_str(include_str!("output_schema.json")).unwrap();
        let result = serde_json::to_value(output).unwrap();
        assert!(jsonschema::is_valid(&schema, &result));
    }
}
