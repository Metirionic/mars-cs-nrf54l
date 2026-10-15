//! Ground-truth decoder for raw captures of the DK COBS ranging stream.
//!
//! Splits the capture on the 0x00 frame delimiter, COBS-decodes each frame,
//! postcard-decodes it as `mars_bluetooth_hci::libc::Serializable`, and reports
//! the CS procedure counter sequence — so byte loss becomes "these exact
//! procedures are missing" instead of "about 9% of the bytes".
//!
//! Usage: cobs-decode <capture.bin>

use mars_bluetooth_hci::libc::Serializable;
use std::collections::BTreeMap;
use std::fs;

/// Decode one COBS frame (delimiter already stripped).
fn cobs_decode(data: &[u8]) -> Option<Vec<u8>> {
    let mut out = Vec::with_capacity(data.len());
    let mut i = 0;
    while i < data.len() {
        let code = data[i] as usize;
        i += 1;
        if code == 0 {
            return None;
        }
        let take = code - 1;
        if i + take > data.len() {
            return None;
        }
        out.extend_from_slice(&data[i..i + take]);
        i += take;
        if code < 0xFF && i < data.len() {
            out.push(0);
        }
    }
    Some(out)
}

fn main() {
    let path = std::env::args().nth(1).expect("usage: cobs-decode <capture.bin>");
    let dump = std::env::args().any(|a| a == "--dump");
    let raw = fs::read(&path).expect("read capture");

    let mut procs: Vec<u16> = Vec::new();
    let mut logs = 0usize;
    let mut malformed = 0usize;
    let mut undecodable = 0usize;
    let mut first_segment = true;
    let segment_count = raw.split(|b| *b == 0).count();
    // For locating damage in the stream: which procedure we last saw and how
    // many frames ago.
    let mut last_proc: Option<u16> = None;
    let mut last_proc_frame_idx: usize = 0;

    for (idx, frame) in raw.split(|b| *b == 0).enumerate() {
        if frame.is_empty() {
            continue;
        }
        let is_last = idx + 1 == segment_count;
        let decoded = match cobs_decode(frame) {
            Some(d) => d,
            None => {
                // The first/last segment of a live capture is a partial frame
                // by construction (the reader attached mid-stream).
                if first_segment || is_last {
                    eprintln!("[partial] edge frame, {} B", frame.len());
                } else {
                    malformed += 1;
                    eprintln!("[malformed] {} B", frame.len());
                }
                first_segment = false;
                continue;
            }
        };
        first_segment = false;
        match postcard::from_bytes::<Serializable>(&decoded) {
            Ok(Serializable::SubeventResultEvent(ev)) => {
                if dump {
                    println!(
                        "{idx},{},{},subevent,{}",
                        frame.len(),
                        decoded.len(),
                        ev.initial_meta.procedure_counter
                    );
                }
                last_proc = Some(ev.initial_meta.procedure_counter);
                last_proc_frame_idx = procs.len();
                procs.push(ev.initial_meta.procedure_counter);
            }
            Ok(Serializable::LogMessage(m)) => {
                if dump {
                    println!("{idx},{},{},log,", frame.len(), decoded.len());
                }
                logs += 1;
                let _ = m;
            }
            Err(e) => {
                if dump {
                    println!("{idx},{},{},undecodable,", frame.len(), decoded.len());
                }
                undecodable += 1;
                let where_ = match last_proc {
                    Some(p) => format!(
                        "at frame #{idx}, {} frames after the frame for procedure {p}",
                        procs.len() - last_proc_frame_idx
                    ),
                    None => format!("at frame #{idx}, before any decodable subevent"),
                };
                eprintln!("[undecodable] {} B payload: {e}  ({where_})", decoded.len());
            }
        }
    }

    let total = procs.len();
    println!("capture        : {path} ({} bytes)", raw.len());
    println!("subevent frames: {total}   log frames: {logs}");
    println!(
        "malformed      : {malformed}   undecodable: {undecodable}"
    );
    if total == 0 {
        println!("VERDICT        : no decodable subevent frames");
        return;
    }

    let first = procs[0];
    let last = *procs.last().unwrap();
    let mut counts: BTreeMap<u16, usize> = BTreeMap::new();
    for p in &procs {
        *counts.entry(*p).or_insert(0) += 1;
    }
    let span = (last as i64 - first as i64 + 1) as usize;

    // Missing procedures inside the observed span = lost on the wire.
    let missing: Vec<u16> = (first..=last).filter(|p| !counts.contains_key(p)).collect();
    let dupes: Vec<(u16, usize)> = counts
        .iter()
        .filter(|(_, c)| **c > 1)
        .map(|(p, c)| (*p, *c))
        .collect();

    println!("procedure range: {first}..={last}  (span {span})");
    println!("frames/procedure: {:.2}", total as f64 / span as f64);
    println!(
        "MISSING procs  : {} {}",
        missing.len(),
        if missing.is_empty() {
            String::new()
        } else {
            format!("-> {:?}", &missing[..missing.len().min(20)])
        }
    );
    println!(
        "DUPLICATE procs: {} {}",
        dupes.len(),
        if dupes.is_empty() {
            String::new()
        } else {
            format!("-> {:?}", &dupes[..dupes.len().min(10)])
        }
    );
}
