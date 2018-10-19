#include "internal.h"

enum {
  RC_LBOARD_START    = 1 << 0,
  RC_LBOARD_CANCEL   = 1 << 1,
  RC_LBOARD_SUBMIT   = 1 << 2,
  RC_LBOARD_VALUE    = 1 << 3,
  RC_LBOARD_PROGRESS = 1 << 4,
  RC_LBOARD_COMPLETE = RC_LBOARD_START | RC_LBOARD_CANCEL | RC_LBOARD_SUBMIT | RC_LBOARD_VALUE
};

void rc_parse_lboard_internal(rc_lboard_t* self, int* ret, void* buffer, void* scratch, const char* memaddr, lua_State* L, int funcs_ndx) {
  int found;

  self->progress = 0;
  found = 0;

  for (;;)
  {
    if ((memaddr[0] == 's' || memaddr[0] == 'S') &&
        (memaddr[1] == 't' || memaddr[1] == 'T') &&
        (memaddr[2] == 'a' || memaddr[2] == 'A') && memaddr[3] == ':') {
      if ((found & RC_LBOARD_START) != 0) {
        *ret = RC_DUPLICATED_START;
        return;
      }

      found |= RC_LBOARD_START;
      memaddr += 4;
      rc_parse_trigger_internal(&self->start, ret, buffer, scratch, &memaddr, L, funcs_ndx);

      if (*ret < 0) {
        return;
      }
    }
    else if ((memaddr[0] == 'c' || memaddr[0] == 'C') &&
             (memaddr[1] == 'a' || memaddr[1] == 'A') &&
             (memaddr[2] == 'n' || memaddr[2] == 'N') && memaddr[3] == ':') {
      if ((found & RC_LBOARD_CANCEL) != 0) {
        *ret = RC_DUPLICATED_CANCEL;
        return;
      }

      found |= RC_LBOARD_CANCEL;
      memaddr += 4;
      rc_parse_trigger_internal(&self->cancel, ret, buffer, scratch, &memaddr, L, funcs_ndx);

      if (*ret < 0) {
        return;
      }
    }
    else if ((memaddr[0] == 's' || memaddr[0] == 'S') &&
             (memaddr[1] == 'u' || memaddr[1] == 'U') &&
             (memaddr[2] == 'b' || memaddr[2] == 'B') && memaddr[3] == ':') {
      if ((found & RC_LBOARD_SUBMIT) != 0) {
        *ret = RC_DUPLICATED_SUBMIT;
        return;
      }

      found |= RC_LBOARD_SUBMIT;
      memaddr += 4;
      rc_parse_trigger_internal(&self->submit, ret, buffer, scratch, &memaddr, L, funcs_ndx);

      if (*ret < 0) {
        return;
      }
    }
    else if ((memaddr[0] == 'v' || memaddr[0] == 'V') &&
             (memaddr[1] == 'a' || memaddr[1] == 'A') &&
             (memaddr[2] == 'l' || memaddr[2] == 'L') && memaddr[3] == ':') {
      if ((found & RC_LBOARD_VALUE) != 0) {
        *ret = RC_DUPLICATED_VALUE;
        return;
      }

      found |= RC_LBOARD_VALUE;
      memaddr += 4;
      rc_parse_value_internal(&self->value, ret, buffer, scratch, &memaddr, L, funcs_ndx);

      if (*ret < 0) {
        return;
      }
    }
    else if ((memaddr[0] == 'p' || memaddr[0] == 'P') &&
             (memaddr[1] == 'r' || memaddr[1] == 'R') &&
             (memaddr[2] == 'o' || memaddr[2] == 'O') && memaddr[3] == ':') {
      if ((found & RC_LBOARD_PROGRESS) != 0) {
        *ret = RC_DUPLICATED_PROGRESS;
        return;
      }

      found |= RC_LBOARD_PROGRESS;
      memaddr += 4;

      self->progress = RC_ALLOC(rc_value_t, buffer, ret, scratch);
      rc_parse_value_internal(self->progress, ret, buffer, scratch, &memaddr, L, funcs_ndx);

      if (*ret < 0) {
        return;
      }
    }
    else {
      *ret = RC_INVALID_LBOARD_FIELD;
      return;
    }

    if (memaddr[0] != ':' || memaddr[1] != ':') {
      break;
    }

    memaddr += 2;
  }

  if ((found & RC_LBOARD_COMPLETE) != RC_LBOARD_COMPLETE) {
    if ((found & RC_LBOARD_START) == 0) {
      *ret = RC_MISSING_START;
    }
    else if ((found & RC_LBOARD_CANCEL) == 0) {
      *ret = RC_MISSING_CANCEL;
    }
    else if ((found & RC_LBOARD_SUBMIT) == 0) {
      *ret = RC_MISSING_SUBMIT;
    }
    else if ((found & RC_LBOARD_VALUE) == 0) {
      *ret = RC_MISSING_VALUE;
    }

    return;
  }

  self->started = self->submitted = 0;
}

int rc_lboard_size(const char* memaddr) {
  int ret;
  rc_lboard_t* self;
  rc_scratch_t scratch;

  ret = 0;
  self = RC_ALLOC(rc_lboard_t, 0, &ret, &scratch);
  rc_parse_lboard_internal(self, &ret, 0, &scratch, memaddr, 0, 0);
  return ret;
}

rc_lboard_t* rc_parse_lboard(void* buffer, const char* memaddr, lua_State* L, int funcs_ndx) {
  int ret;
  rc_lboard_t* self;
  rc_scratch_t scratch;
  
  ret = 0;
  self = RC_ALLOC(rc_lboard_t, buffer, &ret, &scratch);
  rc_parse_lboard_internal(self, &ret, buffer, 0, memaddr, L, funcs_ndx);
  return ret >= 0 ? self : 0;
}

int rc_evaluate_lboard(rc_lboard_t* self, unsigned* value, rc_peek_t peek, void* peek_ud, lua_State* L) {
  int start_ok, cancel_ok, submit_ok;
  int action = -1;

  /* ASSERT: these are always tested once every frame, to ensure delta variables work properly */
  start_ok = rc_test_trigger(&self->start, peek, peek_ud, L);
  cancel_ok = rc_test_trigger(&self->cancel, peek, peek_ud, L);
  submit_ok = rc_test_trigger(&self->submit, peek, peek_ud, L);

  if (self->submitted) {
    /* if we've already submitted or canceled the leaderboard, don't reactivate it until it becomes inactive. */
    if (!start_ok) {
      self->submitted = 0;
    }
  }
  else if (!self->started) {
    /* leaderboard is not active, if the start condition is true, activate it */
    if (start_ok && !cancel_ok) {
      if (submit_ok) {
        /* start and submit both true in the same frame, just submit without announcing the leaderboard is available */
        action = RC_LBOARD_TRIGGERED;
        /* prevent multiple submissions/notifications */
        self->submitted = 1;
      }
      else if (self->start.requirement != 0 || self->start.alternative != 0) {
        self->started = 1;
        action = RC_LBOARD_STARTED;
      }
    }
  }
  else {
    /* leaderboard is active */
    if (cancel_ok) {
      /* cancel condition is true, deactivate the leaderboard */
      self->started = 0;
      action = RC_LBOARD_CANCELED;
      /* prevent multiple cancel notifications */
      self->submitted = 1;
    }
    else if (submit_ok) {
      /* submit condition is true, submit the current value */
      self->started = 0;
      action = RC_LBOARD_TRIGGERED;
      self->submitted = 1;
    }
  }

  if (action == -1) {
    action = self->started ? RC_LBOARD_ACTIVE : RC_LBOARD_INACTIVE;
  }

  /* Calculate the value */
  switch (action) {
    case RC_LBOARD_ACTIVE: /* fall through */
    case RC_LBOARD_STARTED:
      *value = rc_evaluate_value(self->progress != 0 ? self->progress : &self->value, peek, peek_ud, L);
      break;

    case RC_LBOARD_TRIGGERED:
      *value = rc_evaluate_value(&self->value, peek, peek_ud, L);
      break;

    case RC_LBOARD_INACTIVE:
    case RC_LBOARD_CANCELED:
      *value = 0;
      break;
  }

  return action;
}

void rc_reset_lboard(rc_lboard_t* self) {
  self->started = self->submitted = 0;

  rc_reset_trigger(&self->start);
  rc_reset_trigger(&self->submit);
  rc_reset_trigger(&self->cancel);
}
