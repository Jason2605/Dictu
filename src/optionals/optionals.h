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
#include "process.h"

typedef ObjModule *(*BuiltinModule)(DictuVM *vm);

typedef struct {
    char *name;
    BuiltinModule module;
} BuiltinModules;

ObjModule *importBuiltinModule(DictuVM *vm, int index);

int findBuiltinModule(char *name, int length);

#endif //dictu_optionals_h
