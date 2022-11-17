/**
this module should be called like
require()
or
import()

with ability to re-direct our repo url or to import directly/require

we should also be able to define exports e.g. 
exports.function
exports.class
exports.var


import keyword is used as:

import x from y;

there is a global import that returns a promise. See
https://v8.dev/features/dynamic-import

After reviewing v8 import keyword, it's not optimally supported,
let's write our own with object/array destructuring

e.g. 
const {a, b} = uModule('path')
const c = uModule('path')

//specified directory
const {a, b} = uModule('path', 'origin', 'auth-token')

//null passed will trigger a different functionality
uModule().	//or...
uModuleSettings.setDefault({origin:x, auth:y})

uModuleSettings can be limited via exposure control
*/