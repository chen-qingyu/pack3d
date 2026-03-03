use pack3d::algorithm::Algorithm;
use pack3d::entities::{BoxType, Orient, Output};
use pack3d::tool::parse_input;
use std::collections::{HashMap, HashSet};
use std::fs;

fn oriented_dims(lx: i32, ly: i32, lz: i32, orient: Orient) -> (i32, i32, i32) {
    match orient {
        Orient::XYZ => (lx, ly, lz),
        Orient::YXZ => (ly, lx, lz),
        Orient::XZY => (lx, lz, ly),
        Orient::ZXY => (lz, lx, ly),
        Orient::YZX => (ly, lz, lx),
        Orient::ZYX => (lz, ly, lx),
    }
}

fn overlaps(a_min: i32, a_len: i32, b_min: i32, b_len: i32) -> bool {
    a_min < b_min + b_len && b_min < a_min + a_len
}

fn load_cases() -> Vec<(String, Output, HashMap<String, BoxType>, HashSet<String>)> {
    let mut paths: Vec<_> = fs::read_dir("data/tests/")
        .unwrap()
        .map(|entry| entry.unwrap().path())
        .collect();
    paths.sort();

    paths
        .into_iter()
        .map(|input_path| {
            let case_name = input_path.display().to_string();
            let input = parse_input(input_path.to_str().unwrap());
            let output = Algorithm::new(input.clone()).run();
            let box_type_map = input
                .box_types
                .iter()
                .cloned()
                .map(|bt| (bt.id.clone(), bt))
                .collect();
            let input_ids = input.boxes.iter().map(|b| b.id.clone()).collect();
            (case_name, output, box_type_map, input_ids)
        })
        .collect()
}

#[test]
fn test_output_schema() {
    let schema = serde_json::from_str(include_str!("../data/output_schema.json")).unwrap();

    for (case_name, output, _, _) in load_cases() {
        let result = serde_json::to_value(&output).unwrap();
        assert!(
            jsonschema::is_valid(&schema, &result),
            "output json does not match schema, case: {}",
            case_name
        );
    }
}

#[test]
fn test_output_box_ids() {
    for (case_name, output, _, input_ids) in load_cases() {
        let mut output_ids = HashSet::new();
        for container in &output.containers {
            for b in &container.boxes {
                assert!(
                    output_ids.insert(b.id.clone()),
                    "duplicate packed box id: {}, case: {}",
                    b.id,
                    case_name
                );
            }
        }
        for b in &output.unpacked_boxes {
            assert!(
                output_ids.insert(b.id.clone()),
                "duplicate unpacked box id: {}, case: {}",
                b.id,
                case_name
            );
        }
        assert_eq!(
            output_ids, input_ids,
            "box ids mismatch with input, case: {}",
            case_name
        );
    }
}

#[test]
fn test_output_container_boundaries() {
    for (case_name, output, box_type_map, _) in load_cases() {
        for container in &output.containers {
            for b in &container.boxes {
                let bt = &box_type_map[&b.type_id];
                let (dx, dy, dz) = oriented_dims(bt.lx, bt.ly, bt.lz, b.orient);
                let (x, y, z) = (b.x.unwrap(), b.y.unwrap(), b.z.unwrap());
                assert!(
                    x >= 0
                        && y >= 0
                        && z >= 0
                        && x + dx <= container.container_type.lx
                        && y + dy <= container.container_type.ly
                        && z + dz <= container.container_type.lz,
                    "box {} position exceeds container boundary, case: {}",
                    b.id,
                    case_name
                );
            }
        }
    }
}

#[test]
fn test_output_no_overlap() {
    for (case_name, output, box_type_map, _) in load_cases() {
        for container in &output.containers {
            for i in 0..container.boxes.len() {
                for j in i + 1..container.boxes.len() {
                    let a = &container.boxes[i];
                    let b = &container.boxes[j];

                    let a_type = &box_type_map[&a.type_id];
                    let b_type = &box_type_map[&b.type_id];

                    let (alx, aly, alz) = oriented_dims(a_type.lx, a_type.ly, a_type.lz, a.orient);
                    let (blx, bly, blz) = oriented_dims(b_type.lx, b_type.ly, b_type.lz, b.orient);

                    let (ax, ay, az) = (a.x.unwrap(), a.y.unwrap(), a.z.unwrap());
                    let (bx, by, bz) = (b.x.unwrap(), b.y.unwrap(), b.z.unwrap());

                    let overlap = overlaps(ax, alx, bx, blx)
                        && overlaps(ay, aly, by, bly)
                        && overlaps(az, alz, bz, blz);
                    assert!(
                        !overlap,
                        "boxes {} and {} overlap, case: {}",
                        a.id, b.id, case_name
                    );
                }
            }
        }
    }
}

#[test]
fn test_output_rates() {
    for (case_name, output, box_type_map, _) in load_cases() {
        for container in &output.containers {
            let used_volume: i64 = container
                .boxes
                .iter()
                .map(|b| {
                    let bt = &box_type_map[&b.type_id];
                    bt.volume()
                })
                .sum();

            let container_volume = container.container_type.volume();
            let expected_rate = used_volume as f64 / container_volume as f64;
            assert_eq!(
                container.volume_rate, expected_rate,
                "volume_rate mismatch: got {}, expected {}, case: {}",
                container.volume_rate, expected_rate, case_name
            );

            match container.container_type.payload {
                Some(payload) => {
                    let used_weight: f64 = container.boxes.iter().map(|b| b.weight.unwrap()).sum();
                    let expected_weight_rate = used_weight / payload;
                    let actual_weight_rate = container.weight_rate.unwrap();
                    assert_eq!(
                        actual_weight_rate, expected_weight_rate,
                        "weight_rate mismatch: got {}, expected {}, case: {}",
                        actual_weight_rate, expected_weight_rate, case_name
                    );
                }
                None => {
                    assert!(
                        container.weight_rate.is_none(),
                        "weight_rate should be none when payload is not set, case: {}",
                        case_name
                    );
                }
            }
        }
    }
}
