#ifndef dictu_optionals_h
#define dictu_optionals_h

#include "../vm/util.h"
#include "math.h"
#include "env.h"
#include "system.h"
#include "json.h"
#include "http.h"
#include "path.h"
#include "c.h"
#include "datetime.h"
#include "socket.h"
#include "random.h"
#include "base64.h"
#include "hashlib.h"
#include "sqlite.h"

#define GET_SELF_CLASS \
  AS_MODULE(args[-1])

#define SET_ERRNO(module_)                                              \
  defineNativeProperty(vm, &module_->values, "errno", NUMBER_VAL(errno))

#define RESET_ERRNO(module_)                                       \
  defineNativeProperty(vm, &module_->values, "errno", 0)

typedef ObjModule *(*BuiltinModule)(DictuVM *vm);

typedef struct {
    char *name;
    BuiltinModule module;
} BuiltinModules;

ObjModule *importBuiltinModule(DictuVM *vm, int index);

Value getErrno(DictuVM* vm, ObjModule* module);

int findBuiltinModule(char *name, int length);

#endif //dictu_optionals_h
