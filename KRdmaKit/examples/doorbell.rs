#[allow(non_snake_case)]
extern crate KRdmaKit;
use KRdmaKit::{queue_pairs::RcDoorbellHelper, *};

fn main() {
    let mut doorbell_helper = RcDoorbellHelper::create(32);

    let ctx = UDriver::create()
        .expect("failed to query device")
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open_context()
        .expect("failed to create RDMA context");

    // create an RCQP
    let client_port: u8 = 1;
    let mut builder = QueuePairBuilder::new(&ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);

    let client_qp = builder.build_rc().expect("failed to create the client QP");

    // query the address info
    let port_attr = ctx
        .get_port_attr(client_port)
        .expect("Query port attr 1 at device 0 error");

    // check the gid
    let gid = ctx
        .query_gid(client_port, 0)
        .expect("Query the gid of port 1 at device 0 error");

    let qpn = client_qp.inner_qp_num();
    let connected_qp = client_qp
        // FIXME : the rq_psn is a magic number
        .bring_up_rc(port_attr.lid as _, gid, qpn, 3185)
        .expect("failed to bring up RC");

    println!("RC connection passes {:?}", connected_qp.status());

    // now sending a value using the connected QP
    let mr = MemoryRegion::new(ctx.clone(), 4096).expect("failed to allocate MR");

    let remote_offset = 1024;
    let remote_addr = mr.get_virt_addr() + remote_offset;
    let test_buf = mr.get_virt_addr() as *mut [u8; 11];
    unsafe {
        (*test_buf).clone_from_slice("Hello world".as_bytes());
        *((mr.get_virt_addr() + remote_offset + 48) as *mut u64) = 0xbbbb;
        *((mr.get_virt_addr() + remote_offset + 64) as *mut u64) = 0xfff0;
    };

    let _ = doorbell_helper
        .post_send_write(
            &mr,
            0..11,
            false,
            mr.get_virt_addr() + remote_offset + 0,
            mr.rkey().0,
            13,
        )
        .expect("Failed to post send read request");

    let _ = doorbell_helper
        .post_send_write(
            &mr,
            0..11,
            false,
            mr.get_virt_addr() + remote_offset + 16,
            mr.rkey().0,
            14,
        )
        .expect("Failed to post send read request");

    let _ = doorbell_helper
        .post_send_write(
            &mr,
            0..11,
            true,
            mr.get_virt_addr() + remote_offset + 32,
            mr.rkey().0,
            15,
        )
        .expect("Failed to post send read request");

    let _ = doorbell_helper
        .post_send_cas(
            &mr,
            16,
            false,
            mr.get_virt_addr() + remote_offset + 48,
            mr.rkey().0,
            16,
            0xbbbb,
            0xffff,
        )
        .expect("Failed to post send read request");

    let _ = doorbell_helper
        .post_send_cas(
            &mr,
            32,
            false,
            mr.get_virt_addr() + remote_offset + 48,
            mr.rkey().0,
            17,
            0xffff,
            0xaaaa,
        )
        .expect("Failed to post send read request");

    let _ = doorbell_helper
        .post_send_faa(
            &mr,
            48,
            true,
            mr.get_virt_addr() + remote_offset + 64,
            mr.rkey().0,
            18,
            0xf,
        )
        .expect("Failed to post send read request");

    // sending the RDMA request
    doorbell_helper.flush_doorbell(&connected_qp).expect("Gee");

    // sleep 1 second to ensure the RDMA is completed
    std::thread::sleep(std::time::Duration::from_millis(1000));
    let mut completions = [Default::default(); 12];

    // pool the completion
    let mut i = 0;
    loop {
        i += 1;
        if i > 10000 {
            panic!("Too many polls");
        }
        let ret = connected_qp
            .poll_send_cq(&mut completions)
            .expect("failed to poll cq");
        if ret.len() > 0 {
            println!("Polled wc {}", ret.len());
            break;
        }
    }

    println!(
        "sanity check ret {:?} wr_id {}",
        completions[0], completions[0].wr_id
    );

    let test_msg = (remote_addr + 0) as *mut [u8; 11];
    let test_msg =
        std::str::from_utf8(unsafe { &*test_msg }).expect("failed to decode received message");
    println!("check received message: {}", test_msg);

    let test_msg = (remote_addr + 16) as *mut [u8; 11];
    let test_msg =
        std::str::from_utf8(unsafe { &*test_msg }).expect("failed to decode received message");
    println!("check received message: {}", test_msg);

    let test_msg = (remote_addr + 32) as *mut [u8; 11];
    let test_msg =
        std::str::from_utf8(unsafe { &*test_msg }).expect("failed to decode received message");
    println!("check received message: {}", test_msg);

    unsafe {
        let cas = *((mr.get_virt_addr() + remote_offset + 48) as *mut u64);
        let faa = *((mr.get_virt_addr() + remote_offset + 64) as *mut u64);
        println!("cas 0x{:x}", cas);
        println!("faa 0x{:x}", faa);
    }
}
