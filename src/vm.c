#include <stdint.h>
#include <string.h>
#include "vec.h"
#include "vm.h"

tn_entity** tn_entities;

void tn_vm_opcode_set_attr(tn_vm *vm, tn_vm_bytecode bc) {
    tn_vm_value val;
    tn_vm_value parent;
    vec_pop(vm->stack_v, val);
    vec_pop(vm->stack_v, parent);
    tn_entity_attribute attr = parent.as.entity->entity->attrs_v[bc.arg];
    if(val.type == TN_VM_TYPE_ENTITY) {
        tn_entity *ent = val.as.entity->entity;
        tn_entity_attribute *own_attr;
        if(ent->validator) {
            if(!ent->validator(val.as.entity)) {
                // TODO: throw error
            }
        }
        if(attr.name_unicity && val.as.entity->name) {
            // TODO: throw error if name is already used in the parent hashtable
        }
        vec_foreach(own_attr, ent->attrs_v) {
            if(own_attr->mandatory && !((1 << own_attr->index) & val.as.entity->flags)) {
                // TODO: throw error: not all necessary attributes are set
            }
        }
    }
    if(attr.validator) {
        if(!attr.validator(parent.as.entity, val)) {
            // TODO: throw error
        }
    }
    if((1 << attr.index) & val.as.entity->flags) {
        // TODO: throw error: attribute already set*
    }
    if(attr.setter(parent.as.entity, val))
    {
        // TODO: throw error: validation failed
    }
    if(attr.is_name) {
        val.as.entity->name = val.as.string;
    }
    parent.as.entity->flags |= (1 << attr.index);
    vec_push(vm->stack_v, parent);
}

void tn_vm_run(tn_vm *vm)
{
    while(vm->prog_counter < vec_len(vm->prog_v)) {
        tn_vm_bytecode bc = vm->prog_v[vm->prog_counter++];
        switch (bc.opcode) {
            case TN_VM_OPCODE_CREATE_ENTITY:
                tn_vm_value val;
                val.type = TN_VM_TYPE_ENTITY;
                val.as.entity = tn_entities[bc.arg]->create();
                vec_push(vm->stack_v, val);
                break;
            case TN_VM_OPCODE_SET_ATTRIBUTE:
                tn_vm_opcode_set_attr(vm, bc);
                break;
            case TN_VM_OPCODE_CONSTANT:
                vec_push(vm->stack_v,vm->constants_v[bc.arg]);
                break;
            case TN_VM_OPCODE_SET_OPTION:
                tn_vm_value ent;
                vec_pop(vm->stack_v, ent);
                if ((1 << bc.arg) & ent.as.entity->options) {
                    // TODO: throw error: option already set
                } else {
                    ent.as.entity->options |= (1 << bc.arg);
                }
                vec_push(vm->stack_v, ent);
                break;
        }
    }
}

uint32_t tn_vm_add_constant(tn_vm *vm, tn_vm_value val)
{
    vec_push(vm->constants_v, val);
    return vec_len(vm->constants_v) - 1;
}

tn_vm *tn_vm_create()
{
    tn_vm *vm;
    tn_vm_value val;
    tn_entity **child;
    vm = malloc(sizeof(tn_vm));
    vm->prog_counter = 0;
    vm->constants_v = NULL;
    vm->prog_v = NULL;
    vm->stack_v = NULL;
    vec_foreach(child,tn_entities) {
        if(!strcmp((*child)->name,"root")) {
            break;
        }
    }
    val.type = TN_VM_TYPE_ENTITY;
    val.as.entity = tn_entities[(*child)->index]->create();
    vec_push(vm->stack_v, val);
    return vm;
}

void *tn_vm_top(tn_vm *vm)
{
    return vm->stack_v[vec_len(vm->stack_v)-1].as.entity;
}