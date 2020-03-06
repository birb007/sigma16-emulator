#include <byteswap.h>
#include <string.h>
#include "vm.h"
#include "cpu.h"
#include "instructions.h"
#include "tracing.h"

static int select_bit(uint16_t val, uint8_t bit_pos) {
    return (val>>15-bit_pos)&0x1;
}

static uint16_t compute_rx_eaddr(sigma16_vm_t* vm) {
    vm->cpu.adr = vm->cpu.regs[vm->cpu.ir.rx.sa] + vm->cpu.ir.rx.disp;
    return vm->cpu.adr;
}


#define SAFE_UPDATE(vm, dst, val) \
    if (dst != 0) \
        vm->cpu.regs[dst] = val;

#define APPLY_OP_RRR(vm, name, op) \
    INTERP_INST(vm, rrr); \
    trace_rrr(vm, name); \
    if (vm->cpu.ir.rrr.d != 0) {\
        SAFE_UPDATE(vm, \
                vm->cpu.ir.rrr.d, \
                vm->cpu.regs[vm->cpu.ir.rrr.sa] op vm->cpu.regs[vm->cpu.ir.rrr.sb]); \
    } \
    vm->cpu.pc += sizeof vm->cpu.ir.rrr >> 1;

#define INTERP_INST(vm, type) \
    vm->cpu.ir.type = *(sigma16_inst_##type##_t*)&vm->mem[vm->cpu.pc]

#define INTERP_RX(vm) \
    INTERP_INST(vm, rx); \
    vm->cpu.ir.rx.disp = bswap_16(vm->cpu.ir.rx.disp)

#define SETFLAG(reg, flag, val) \
    (*(sigma16_reg_status_t*)&reg).flag = val

#define CLEARFLAGS(reg) \
    memset(&reg, 0, sizeof(sigma16_reg_status_t));


void write_mem(sigma16_vm_t* vm, uint16_t addr, uint16_t val) {
    vm->mem[addr] = bswap_16(val);
}

uint16_t read_mem(sigma16_vm_t* vm, uint16_t addr) {
    return bswap_16(vm->mem[addr]);
}

int sigma16_vm_init(sigma16_vm_t** vm, char* fname) {
    FILE* executable;
    size_t exec_size;

    *vm = calloc(1, sizeof **vm);
    if (!*vm) {
        perror("unable to allocate memory for vm");
        return -1;
    }

    if (!(executable = fopen(fname, "rb"))) {
        perror("unable to open file");
        return -1;
    }

    fseek(executable, 0L, SEEK_END);
    exec_size = ftell(executable);
    fseek(executable, 0L, SEEK_SET);

    if (!((*vm)->mem = malloc(exec_size))) {
        perror("unable to allocate vm memory");
        goto error;
    }

    fread((*vm)->mem, exec_size, 1, executable);
    return 0;
error:
    free(*vm);
    return -1;
}

void sigma16_vm_del(sigma16_vm_t* vm) {
    free(vm->mem);
    free(vm);
}

int sigma16_vm_exec(sigma16_vm_t* vm) {

    static const void* dispatch_table[] = {
        &&do_add, &&do_sub, &&do_mul, &&do_div, &&do_cmp, &&do_cmplt, &&do_cmpeq,
        &&do_cmpgt, &&do_invold, &&do_andold, &&do_orold, &&do_xorold, &&do_nop,
        &&do_trap, &&do_decode_exp, &&do_decode_rx
    };

    static const void* exp_dispatch_table[] = {
        &&do_rfi
    };

    static const void* rx_dispatch_table[] = {
        &&do_lea, &&do_load, &&do_store, &&do_jump, &&do_jumpc0, &&do_jumpc1,
        &&do_jumpf, &&do_jumpt, &&do_jal
    };

    puts("============= CPU TRACE ============= ");
#define DISPATCH() goto *dispatch_table[(vm->mem[vm->cpu.pc]>>4)&0xf]
    DISPATCH();

do_add:
    APPLY_OP_RRR(vm, "add", +);

    CLEARFLAGS(vm->cpu.regs[15]);
    SETFLAG(vm->cpu.regs[15], G, vm->cpu.ir.rrr.d > 0);
    SETFLAG(vm->cpu.regs[15], g, (int16_t)vm->cpu.ir.rrr.d > 0); SETFLAG(vm->cpu.regs[15], E, vm->cpu.ir.rrr.d == 0); SETFLAG(vm->cpu.regs[15], L, (int16_t)vm->cpu.ir.rrr.d < 0);
    SETFLAG(vm->cpu.regs[15], L, vm->cpu.ir.rrr.d == 0);
    // TODO overflow & carry
    SETFLAG(vm->cpu.regs[15], V, vm->cpu.ir.rrr.d > 0);
    SETFLAG(vm->cpu.regs[15], v, vm->cpu.ir.rrr.d > 0);
    SETFLAG(vm->cpu.regs[15], C, vm->cpu.ir.rrr.d > 0);

    DISPATCH();
do_sub:
    APPLY_OP_RRR(vm, "sub", -);
    DISPATCH();
do_mul:
    APPLY_OP_RRR(vm, "mul", *);
    DISPATCH();
do_div:
    APPLY_OP_RRR(vm, "div", /);
    DISPATCH();
do_cmp:
    INTERP_INST(vm, rrr);
    trace_cmp(vm);

    CLEARFLAGS(vm->cpu.regs[15]);
    uint16_t a = vm->cpu.regs[vm->cpu.ir.rrr.sa];
    uint16_t b = vm->cpu.regs[vm->cpu.ir.rrr.sb];
    SETFLAG(vm->cpu.regs[15], G, a > b);
    SETFLAG(vm->cpu.regs[15], g, (int16_t)a > (int16_t)b);
    SETFLAG(vm->cpu.regs[15], E, a == b);
    SETFLAG(vm->cpu.regs[15], l, (int16_t)a < (int16_t)b);
    SETFLAG(vm->cpu.regs[15], L, a < b);

    vm->cpu.pc += sizeof vm->cpu.ir.rrr >> 1;
    DISPATCH();
do_cmplt:
    APPLY_OP_RRR(vm, "cmplt", <);

    CLEARFLAGS(vm->cpu.regs[15]);
    DISPATCH();
do_cmpeq:
    APPLY_OP_RRR(vm, "cmpeq", ==);

    CLEARFLAGS(vm->cpu.regs[15]);
    DISPATCH();
do_cmpgt:
    APPLY_OP_RRR(vm, "cmpgt", >);

    CLEARFLAGS(vm->cpu.regs[15]);
    DISPATCH();
do_invold:
    // APPLY_OP_RRR(vm, ~);
    INTERP_INST(vm, rrr);
    trace_rrr(vm, "inv");

    CLEARFLAGS(vm->cpu.regs[15]);
    DISPATCH();
do_andold:
    APPLY_OP_RRR(vm, "and", &);

    CLEARFLAGS(vm->cpu.regs[15]);
    DISPATCH();
do_orold:
    APPLY_OP_RRR(vm, "or", |);

    CLEARFLAGS(vm->cpu.regs[15]);
    DISPATCH();
do_xorold:
    APPLY_OP_RRR(vm, "xor", ^);

    CLEARFLAGS(vm->cpu.regs[15]);
    DISPATCH();
do_nop:
    INTERP_INST(vm, rrr);
    trace_rrr(vm, "nop");
    vm->cpu.pc += sizeof vm->cpu.ir.rrr >> 1;

    CLEARFLAGS(vm->cpu.regs[15]);
    DISPATCH();
do_trap:
    INTERP_INST(vm, rrr);
    trace_trap(vm);
    goto end_hotloop;

    CLEARFLAGS(vm->cpu.regs[15]);
    DISPATCH();
do_decode_exp:
    INTERP_INST(vm, exp0);
    goto *exp_dispatch_table[vm->cpu.ir.exp0.ab];
do_rfi:
    printf("[%04x]\trfi\n", vm->cpu.pc);
    // TODO rfi
    vm->cpu.pc += sizeof vm->cpu.ir.exp0 >> 1;
    DISPATCH();
// TODO rest of exp instructions
do_decode_rx:
    INTERP_RX(vm);
    goto *rx_dispatch_table[vm->cpu.ir.rx.sb];
do_lea:
    trace_rx(vm, "lea");
    SAFE_UPDATE(vm, vm->cpu.ir.rx.d, compute_rx_eaddr(vm));
    vm->cpu.pc += sizeof vm->cpu.ir.rx >> 1;
    DISPATCH();
do_load:
    trace_rx(vm, "load");
    SAFE_UPDATE(vm, vm->cpu.ir.rx.d, read_mem(vm, compute_rx_eaddr(vm)));
    vm->cpu.pc += sizeof vm->cpu.ir.rx >> 1;
    DISPATCH();
do_store:
    trace_rx(vm, "store");
    write_mem(vm, compute_rx_eaddr(vm), vm->cpu.regs[vm->cpu.ir.rx.d]);
    vm->cpu.pc += sizeof vm->cpu.ir.rx >> 1;
    DISPATCH();
// TODO rest of rx instructions
do_jump:
    trace_branch(vm, "jump");
    vm->cpu.pc = compute_rx_eaddr(vm);
    DISPATCH();
do_jumpc0:
    trace_branch(vm, "jumpc0");
    if (!select_bit(vm->cpu.regs[15], vm->cpu.ir.rx.d)) {
        vm->cpu.pc = compute_rx_eaddr(vm);
    } else {
        vm->cpu.pc += sizeof vm->cpu.ir.rx >> 1;
    }
    DISPATCH();
do_jumpc1:
    trace_branch(vm, "jumpc1");
    if (select_bit(vm->cpu.regs[15], vm->cpu.ir.rx.d)) {
        vm->cpu.pc = compute_rx_eaddr(vm);
    } else {
        vm->cpu.pc += sizeof vm->cpu.ir.rx >> 1;
    }
    DISPATCH();
do_jumpf:
    trace_branch(vm, "jumpf");
    vm->cpu.pc = (!vm->cpu.ir.rx.d ? compute_rx_eaddr(vm) : vm->cpu.pc + sizeof vm->cpu.ir.rx >> 1);
    DISPATCH();
do_jumpt:
    trace_branch(vm, "jumpt");
    vm->cpu.pc = (vm->cpu.ir.rx.d ? compute_rx_eaddr(vm) : vm->cpu.pc + sizeof vm->cpu.ir.rx >> 1);
    DISPATCH();
do_jal:
    trace_branch(vm, "jal");
    SAFE_UPDATE(vm, vm->cpu.ir.rx.d, vm->cpu.pc + (sizeof vm->cpu.ir.rx >> 1));
    vm->cpu.pc = compute_rx_eaddr(vm);
    DISPATCH();
end_hotloop:
    dump_cpu(&vm->cpu);
    return 0;
error:
    return -1;
}
