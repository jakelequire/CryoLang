use criterion::{black_box, criterion_group, criterion_main, Criterion};
use cryofmt::{format_string, FormatOptions};

fn benchmark_variable_formatting(c: &mut Criterion) {
    let input = r#"
        const   x:int=42;
        mut     y:string="hello world";
        const   PI:float=3.14159;
        mut     flag:boolean=true;
    "#;
    
    let options = FormatOptions::default();
    
    c.bench_function("format_variables", |b| {
        b.iter(|| format_string(black_box(input), black_box(&options)))
    });
}

fn benchmark_function_formatting(c: &mut Criterion) {
    let input = r#"
        function main()->int{return 0;}
        function add(a:int,b:int)->int{return a+b;}
        function complex(x:int,y:string,z:boolean)->int{
            if(z){return x+1;}else{return x-1;}
        }
    "#;
    
    let options = FormatOptions::default();
    
    c.bench_function("format_functions", |b| {
        b.iter(|| format_string(black_box(input), black_box(&options)))
    });
}

fn benchmark_struct_formatting(c: &mut Criterion) {
    let input = r#"
        type struct Point{x:int,y:int}
        type struct Person{name:string,age:int,email:string}
        type struct Complex{
            real:float,imaginary:float,metadata:string
        }
    "#;
    
    let options = FormatOptions::default();
    
    c.bench_function("format_structs", |b| {
        b.iter(|| format_string(black_box(input), black_box(&options)))
    });
}

fn benchmark_large_file(c: &mut Criterion) {
    let mut input = String::new();
    
    // Generate a large file with mixed constructs
    for i in 0..100 {
        input.push_str(&format!(r#"
            const var{}: int = {};
            function func{}(param: int) -> int {{
                if (param > 0) {{
                    return param + 1;
                }} else {{
                    return param - 1;
                }}
            }}
            type struct Struct{} {{
                field1: int,
                field2: string
            }}
        "#, i, i * 10, i, i));
    }
    
    let options = FormatOptions::default();
    
    c.bench_function("format_large_file", |b| {
        b.iter(|| format_string(black_box(&input), black_box(&options)))
    });
}

criterion_group!(
    benches,
    benchmark_variable_formatting,
    benchmark_function_formatting,
    benchmark_struct_formatting,
    benchmark_large_file
);
criterion_main!(benches);