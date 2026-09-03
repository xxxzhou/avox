import struct, os, sys

def parse_dump(path):
    with open(path, 'rb') as f:
        data = f.read()
    sig = struct.unpack_from('<I', data, 0)[0]
    ver = struct.unpack_from('<I', data, 4)[0]
    nstreams = struct.unpack_from('<I', data, 8)[0]
    dir_rva = struct.unpack_from('<I', data, 12)[0]
    print(f'sig=0x{sig:08x} ver=0x{ver:08x} streams={nstreams}')
    streams = {}
    for i in range(nstreams):
        st, dsz, rva = struct.unpack_from('<III', data, dir_rva + i * 12)
        streams[st] = (dsz, rva)
    if 6 not in streams:
        print('no exception stream')
        return
    dsz, rva = streams[6]
    tid = struct.unpack_from('<I', data, rva)[0]
    ex = rva + 8
    exc_code = struct.unpack_from('<I', data, ex)[0]
    exc_addr = struct.unpack_from('<Q', data, ex + 16)[0]
    ctx_loc = rva + 8 + 152
    ctx_dsz, ctx_rva = struct.unpack_from('<II', data, ctx_loc)
    base = ctx_rva
    rax = struct.unpack_from('<Q', data, base + 0x78)[0]
    rsp = struct.unpack_from('<Q', data, base + 0x98)[0]
    rbp = struct.unpack_from('<Q', data, base + 0xA0)[0]
    rip = struct.unpack_from('<Q', data, base + 0xF8)[0]
    print(f'--- Exception ---')
    print(f'thread_id={tid} code=0x{exc_code:08x} addr=0x{exc_addr:x}')
    print(f'RIP=0x{rip:x} RSP=0x{rsp:x} RBP=0x{rbp:x} RAX=0x{rax:x}')
    mods = []
    if 4 in streams:
        mdsz, mrva = streams[4]
        nmods = struct.unpack_from('<I', data, mrva)[0]
        for j in range(nmods):
            ent = mrva + 4 + j * 108
            base2 = struct.unpack_from('<Q', data, ent)[0]
            size = struct.unpack_from('<Q', data, ent + 8)[0]
            name_rva = struct.unpack_from('<I', data, ent + 0x14)[0]
            name = ''
            if name_rva:
                try:
                    nlen = struct.unpack_from('<I', data, name_rva)[0]
                    name = data[name_rva + 4:name_rva + 4 + nlen].decode('utf-16-le', 'ignore').rstrip('\x00')
                except Exception:
                    pass
            mods.append((base2, size, name))
    for m in sorted(mods, key=lambda x: -x[0]):
        b, sz, nm = m
        if rip and b <= rip < b + sz:
            print(f'  >>> RIP in {os.path.basename(nm)} base=0x{b:x} off=0x{rip-b:x}')
        if exc_addr and b <= exc_addr < b + sz:
            print(f'  >>> EXC in {os.path.basename(nm)} base=0x{b:x} off=0x{exc_addr-b:x}')

for p in sys.argv[1:]:
    print(f'===== {os.path.basename(p)} =====')
    parse_dump(p)
