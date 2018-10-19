#include "internal.h"

#include <stdlib.h>

rc_condition_t* rc_parse_condition(int* ret, void* buffer, rc_scratch_t* scratch, const char** memaddr, lua_State* L, int funcs_ndx) {
  rc_condition_t* self;
  const char* aux;
  int ret2;

  aux = *memaddr;
  self = RC_ALLOC(rc_condition_t, buffer, ret, scratch);
  self->current_hits = 0;

  if (*aux != 0 && aux[1] == ':') {
    switch (*aux) {
      case 'p': case 'P': self->type = RC_CONDITION_PAUSE_IF; break;
      case 'r': case 'R': self->type = RC_CONDITION_RESET_IF; break;
      case 'a': case 'A': self->type = RC_CONDITION_ADD_SOURCE; break;
      case 'b': case 'B': self->type = RC_CONDITION_SUB_SOURCE; break;
      case 'c': case 'C': self->type = RC_CONDITION_ADD_HITS; break;
      default: *ret = RC_INVALID_CONDITION_TYPE; return 0;
    }

    aux += 2;
  }
  else {
    self->type = RC_CONDITION_STANDARD;
  }

  ret2 = rc_parse_operand(&self->operand1, &aux, 1, L, funcs_ndx);

  if (ret2 < 0) {
    *ret = ret2;
    return 0;
  }

  switch (*aux++) {
    case '=':
      self->oper = RC_CONDITION_EQ;
      aux += *aux == '=';
      break;
    
    case '!':
      if (*aux++ != '=') {
        /* fall through */
    default:
        *ret = RC_INVALID_OPERATOR;
        return 0;
      }

      self->oper = RC_CONDITION_NE;
      break;
    
    case '<':
      self->oper = RC_CONDITION_LT;

      if (*aux == '=') {
        self->oper = RC_CONDITION_LE;
        aux++;
      }

      break;
    
    case '>':
      self->oper = RC_CONDITION_GT;

      if (*aux == '=') {
        self->oper = RC_CONDITION_GE;
        aux++;
      }

      break;
  }

  ret2 = rc_parse_operand(&self->operand2, &aux, 1, L, funcs_ndx);

  if (ret2 < 0) {
    *ret = ret2;
    return 0;
  }

  if (*aux == '(') {
    char* end;
    self->required_hits = (unsigned)strtoul(++aux, &end, 10);

    if (end == aux || *end != ')') {
      *ret = RC_INVALID_REQUIRED_HITS;
      return 0;
    }

    aux = end + 1;
  }
  else if (*aux == '.') {
    char* end;
    self->required_hits = (unsigned)strtoul(++aux, &end, 10);

    if (end == aux || *end != '.') {
      *ret = RC_INVALID_REQUIRED_HITS;
      return 0;
    }

    aux = end + 1;
  }
  else {
    self->required_hits = 0;
  }

  *memaddr = aux;
  return self;
}

int rc_test_condition(rc_condition_t* self, unsigned add_buffer, rc_peek_t peek, void* ud, lua_State* L) {
  unsigned value1 = rc_evaluate_operand(&self->operand1, peek, ud, L) + add_buffer;
  unsigned value2 = rc_evaluate_operand(&self->operand2, peek, ud, L);

  switch (self->oper) {
    case RC_CONDITION_EQ: return value1 == value2;
    case RC_CONDITION_NE: return value1 != value2;
    case RC_CONDITION_LT: return value1 < value2;
    case RC_CONDITION_LE: return value1 <= value2;
    case RC_CONDITION_GT: return value1 > value2;
    case RC_CONDITION_GE: return value1 >= value2;
    default: return 1;
  }
}
