#include <idc.idc>

static describe(out, ea, label)
{
    auto x, start, end, p;
    set_name(ea, label, SN_CHECK | SN_NOWARN);
    start = get_func_attr(ea, FUNCATTR_START);
    end = get_func_attr(ea, FUNCATTR_END);
    fprintf(out, "\n%s entry=%08X function=%08X..%08X\n", label, ea, start, end);
    x = get_first_cref_to(ea);
    while (x != BADADDR) {
        fprintf(out, "  code reference from %08X %s\n", x, get_func_name(x));
        x = get_next_cref_to(ea, x);
    }
    if (start == BADADDR) { start = ea; end = ea + 128; }
    if (end - start > 4096) end = start + 4096;
    for (p = start; p != BADADDR && p < end; p = next_head(p, end))
        fprintf(out, "%08X %s\n", p, generate_disasm_line(p, 0));
}

static main()
{
    auto f, out, size, base;
    batch(1);
    if (!set_processor_type("SH4", SETPROC_USER)) qexit(20);
    set_inf_attr(INF_LFLAGS, get_inf_attr(INF_LFLAGS) & ~LFLG_MSF);
    base = 0x8C010000;
    f = fopen("1ST_READ.BIN", "rb");
    if (f == 0) qexit(21);
    fseek(f, 0, 2); size = ftell(f);
    if (size != 1809817) qexit(22);
    add_segm_ex(base, base + size, 0, 1, 0, 0, ADDSEG_NOSREG | ADDSEG_OR_DIE);
    set_segm_name(base, "MVC2_BOOT");
    set_segm_class(base, "CODE");
    if (!loadfile(f, 0, base, size)) qexit(23);
    fclose(f);
    add_segm_ex((base + size + 4095) & ~4095, 0x8D000000, 0, 1, 0, 0, ADDSEG_NOSREG | ADDSEG_OR_DIE);
    set_segm_name(0x8C200000, "MVC2_BSS_RAM");
    set_segm_class(0x8C200000, "BSS");
    add_entry(base, base, "mvc2_boot", 1);
    // Independent known prologue for this LZ family; loop entries below are
    // labels, not fabricated whole-function boundaries.
    create_insn(0x8C0354C0);
    plan_and_wait(0x8C0354C0, 0x8C03552A);
    if (!add_func(0x8C0354C0, 0x8C03552A))
        msg("LZ function boundary requires manual review\n");
    auto_wait();
    out = fopen("mvc2-hot-functions.txt", "w");
    fprintf(out, "Processor=%s input-size=%d base=%08X\n", get_processor_name(), size, base);
    describe(out, 0x8C0354C0, "mvc2_lz_decompress");
    describe(out, 0x8C0354D4, "mvc2_lz_token_loop");
    describe(out, 0x8C02DEC6, "mvc2_cache_invalidate_loop");
    describe(out, 0x8C1291E2, "mvc2_udiv32_hot_entry");
    describe(out, 0x8C12FDB6, "mvc2_vertex_transform_hot_entry");
    describe(out, 0x8C12FE18, "mvc2_vertex_perspective_hot_entry");
    describe(out, 0x8C12FE5A, "mvc2_vertex_sq_emit_hot_entry");
    describe(out, 0x8C17A6F0, "mvc2_copy_hot_entry");
    fclose(out);
    auto_wait();
    save_database("mvc2-us-v1001.i64", 0);
    qexit(0);
}
