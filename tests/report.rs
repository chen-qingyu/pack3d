use pack3d::algorithm::Algorithm;
use pack3d::tool::parse_input;
use peak_alloc::PeakAlloc;
use std::fs;
use std::io::Write;
use std::time::Instant;

#[global_allocator]
static PEAK_ALLOC: PeakAlloc = PeakAlloc;

// 文件测试结果
struct FileResult {
    filename: String, // 文件名
    rate: f64,        // 体积利用率
    duration: f64,    // 耗时(秒)
    memory: f32,      // 内存占用(KB)
}

#[test]
fn report() {
    colog::init();
    if fs::exists("data/br_json/").unwrap() == false {
        log::info!("data/br_json/ not found, skipping report test.");
        return; // 若输入数据不存在则跳过测试
    }

    // 单线程顺序处理每个文件，避免多线程导致内存统计不准
    let mut report = Vec::new();
    for entry in fs::read_dir("data/br_json/").unwrap() {
        log::set_max_level(log::LevelFilter::Off);
        let path = entry.unwrap().path();

        // 记录起始时间和内存
        PEAK_ALLOC.reset_peak_usage();
        let mem_before = PEAK_ALLOC.current_usage_as_kb();
        let start_time = Instant::now();

        // 读取输入文件
        let input = parse_input(path.to_str().unwrap());

        // 执行装箱算法
        let output = Algorithm::new(input).run();

        // 记录各项指标
        let end_time = Instant::now();
        let mem_after = PEAK_ALLOC.peak_usage_as_kb();
        let filename = path.file_name().unwrap().to_string_lossy().to_string();
        let rate = output.containers[0].volume_rate * 100.0;
        let duration = (end_time - start_time).as_secs_f64();
        let memory = mem_after - mem_before;

        report.push(FileResult {
            filename: filename.clone(),
            rate,
            duration,
            memory,
        });

        // 临时启用日志输出
        log::set_max_level(log::LevelFilter::Info);
        log::info!(
            "{} - rate: {:.2}%, duration: {:.3} s, memory: {:.0} KB",
            filename,
            rate,
            duration,
            memory
        );
    }

    // 写入CSV
    let mut csv = fs::File::create("tests/report.csv").unwrap();
    writeln!(csv, "filename,volume_rate(%),duration(s),memory(KB)").unwrap();
    for result in &report {
        writeln!(
            csv,
            "{},{:.2},{:.3},{:.0}",
            result.filename, result.rate, result.duration, result.memory
        )
        .unwrap();
    }
}
