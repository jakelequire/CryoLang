use std::time::{Duration, Instant};
use std::collections::HashMap;

#[derive(Debug, Default)]
pub struct ProfilerStats {
    pub total_files: usize,
    pub total_lines: usize,
    pub total_chars: usize,
    pub total_time: Duration,
    pub lexing_time: Duration,
    pub parsing_time: Duration,
    pub formatting_time: Duration,
    pub per_file_times: Vec<Duration>,
}

pub struct Profiler {
    start_time: Option<Instant>,
    phase_times: HashMap<String, Duration>,
    current_phase: Option<String>,
    phase_start: Option<Instant>,
}

impl Default for Profiler {
    fn default() -> Self {
        Self::new()
    }
}

impl Profiler {
    pub fn new() -> Self {
        Profiler {
            start_time: None,
            phase_times: HashMap::new(),
            current_phase: None,
            phase_start: None,
        }
    }

    pub fn start(&mut self) {
        self.start_time = Some(Instant::now());
    }

    pub fn start_phase(&mut self, phase: &str) {
        if let Some(current) = &self.current_phase.clone() {
            self.end_phase(current);
        }
        
        self.current_phase = Some(phase.to_string());
        self.phase_start = Some(Instant::now());
    }

    pub fn end_phase(&mut self, phase: &str) {
        if let Some(start) = self.phase_start.take() {
            let duration = start.elapsed();
            *self.phase_times.entry(phase.to_string()).or_insert(Duration::ZERO) += duration;
        }
        self.current_phase = None;
    }

    pub fn total_time(&self) -> Duration {
        self.start_time.map(|start| start.elapsed()).unwrap_or(Duration::ZERO)
    }

    pub fn phase_time(&self, phase: &str) -> Duration {
        self.phase_times.get(phase).cloned().unwrap_or(Duration::ZERO)
    }

    pub fn print_stats(&self) {
        println!("=== CryoFormat Performance Stats ===");
        println!("Total time: {:?}", self.total_time());
        
        for (phase, duration) in &self.phase_times {
            println!("{}: {:?}", phase, duration);
        }
    }

    pub fn generate_stats(&self, files_processed: usize, lines_processed: usize, chars_processed: usize) -> ProfilerStats {
        ProfilerStats {
            total_files: files_processed,
            total_lines: lines_processed,
            total_chars: chars_processed,
            total_time: self.total_time(),
            lexing_time: self.phase_time("lexing"),
            parsing_time: self.phase_time("parsing"),
            formatting_time: self.phase_time("formatting"),
            per_file_times: vec![], // Would need to track this separately
        }
    }
}

impl ProfilerStats {
    pub fn files_per_second(&self) -> f64 {
        if self.total_time.as_secs_f64() > 0.0 {
            self.total_files as f64 / self.total_time.as_secs_f64()
        } else {
            0.0
        }
    }

    pub fn lines_per_second(&self) -> f64 {
        if self.total_time.as_secs_f64() > 0.0 {
            self.total_lines as f64 / self.total_time.as_secs_f64()
        } else {
            0.0
        }
    }

    pub fn chars_per_second(&self) -> f64 {
        if self.total_time.as_secs_f64() > 0.0 {
            self.total_chars as f64 / self.total_time.as_secs_f64()
        } else {
            0.0
        }
    }

    pub fn print_performance_report(&self) {
        println!("\n=== Performance Report ===");
        println!("Files processed: {}", self.total_files);
        println!("Lines processed: {}", self.total_lines);
        println!("Characters processed: {}", self.total_chars);
        println!("Total time: {:?}", self.total_time);
        println!("Files/second: {:.2}", self.files_per_second());
        println!("Lines/second: {:.0}", self.lines_per_second());
        println!("Characters/second: {:.0}", self.chars_per_second());
        println!("\n=== Phase Breakdown ===");
        println!("Lexing: {:?} ({:.1}%)", self.lexing_time, self.phase_percentage(self.lexing_time));
        println!("Parsing: {:?} ({:.1}%)", self.parsing_time, self.phase_percentage(self.parsing_time));
        println!("Formatting: {:?} ({:.1}%)", self.formatting_time, self.phase_percentage(self.formatting_time));
    }

    fn phase_percentage(&self, phase_time: Duration) -> f64 {
        if self.total_time.as_secs_f64() > 0.0 {
            (phase_time.as_secs_f64() / self.total_time.as_secs_f64()) * 100.0
        } else {
            0.0
        }
    }
}