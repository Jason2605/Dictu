---
layout: default
title: Error Handling
nav_order: 14
---

# Error Handling
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

## Error Handling

Dictu does not have exceptions like many other languages, and instead
uses a Result type. A Result is a type which can be in one of two states,
SUCCESS or ERROR. Logic which may return an error will always return a Result
type which will wrap a value on success or wrap a string on failure with a given
error message. This wrapped value *must* be unwrapped before accessing it.

### Result type
Note, if returning a Result type from a function there is nothing in the interpreter
that will enforce both Success and Error types can be returned, or even that these are the only
types that can be returned, however it is very much recommended that if you return a Result type
from a function, this is the only type you ever return - this will made handling a result type
much easier for the caller.

#### Success
Creating a Success type is incredibly simple with the builtin `Success()` function.
Any type can be passed to Success to be wrapped.

```cs
var result = Success(10);
print(result.unwrap()); // 10
```

#### Error
Creating an Error type is incredibly simple with the builtin `Error()` function.
Only a string can be passed to Success to be wrapped.

```cs
var result = Error("Some error happened!!");
print(result.unwrapError()); // 'Some error happened!!'
```

### .unwrap()

As previously explained to get a value out of a Result it needs to be unwrapped.
If you attempt to unwrap a Result that is of ERROR status a runtime error will be raised.

```cs
var num = "10".toNumber();
print(num); // <Result Suc>
print(num.unwrap()); // 10
```

### .unwrapError()

A Result that has a type of ERROR will always contain an error message as to why it failed, however 
attempting to unwrap a Result that is an ERROR gives you a runtime error. Instead you must use
`.unwrapError()`. Attempting to use `unwrapError` on a Result with type SUCCESS will raise a runtime
error.

```cs
"num".toNumber().unwrapError(); // 'Can not convert 'num' to number'
```

### .success()

Check if a Result type is in a SUCCESS state, returns a boolean.

```cs
"10".toNumber().success(); // true
"number".toNumber().success(); // false
```

### .match(func: success, func: error)

`.match` takes two callbacks that are ran depending upon the status of the result type. The callbacks passed to
match must both have one parameter each, on success the unwrapped value is passed as the first argument and on
error the unwrapError reason is passed to the failure callback. The value returned from `.match()` is the value
returned from the user defined callback.

```cs
var number = "10".toNumber().match(
    def (result) => result,
    def (error) => {
        print(error);
        System.exit(1);
    }
);

print(number); // 10

var number = "number".toNumber().match(
    def (result) => result,
    def (error) => {
        print(error); // Can not convert 'number' to number
        System.exit(1);
    }
);

print(number);
```