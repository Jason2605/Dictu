#ifndef dictu_json_h
#define dictu_json_h

#include "json/jsonParseLib.h"
#include "json/jsonBuilderLib.h"
#include "optionals.h"
#include "../vm/vm.h"

ObjModule *createJSONModule(DictuVM *vm);

#endif //dictu_json_h
