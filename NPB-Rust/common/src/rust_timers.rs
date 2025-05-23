use std::sync::Mutex;

use crate::pega_tempo;

static START: Mutex<[f64; 64]> = Mutex::new([0.0; 64]);
static ELAPSED: Mutex<[f64; 64]> = Mutex::new([0.0; 64]);
pub fn timer_clear(x:usize){
    let mut elapsed = ELAPSED.lock().unwrap();
    elapsed[x] = 0.0;
}
pub fn timer_start(x:usize){
    let mut start = START.lock().unwrap();
    start[x] = pega_tempo::wtime();
}
pub fn timer_stop(x:usize){
    let start_time;
    {
        let start = START.lock().unwrap();
        start_time = start[x];
    }
    let now:f64 = pega_tempo::wtime();
    let elapse = now - start_time;
    let mut elapsed = ELAPSED.lock().unwrap();
    elapsed[x] += elapse;
}

pub fn timer_read(x:usize) -> f64{
    let elapsed = ELAPSED.lock().unwrap();
    elapsed[x]
}
