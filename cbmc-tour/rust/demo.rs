// Plain Rust. No C anywhere in this file.
fn mid(lo: u32, hi: u32) -> u32 {
    (lo + hi) / 2          // same overflow bug as the C example
}

#[kani::proof]
fn check_mid() {
    let lo: u32 = kani::any();       // symbolic input, like nondet_uint()
    let hi: u32 = kani::any();
    kani::assume(lo <= hi);          // like __CPROVER_assume
    let m = mid(lo, hi);
    assert!(lo <= m && m <= hi);
}
