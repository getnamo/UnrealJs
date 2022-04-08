[![star this repo](http://githubbadges.com/star.svg?user=ncsoft&repo=Unreal.js&style=default)](https://github.com/ncsoft/Unreal.js)
[![fork this repo](http://githubbadges.com/fork.svg?user=ncsoft&repo=Unreal.js&style=default)](https://github.com/ncsoft/Unreal.js/fork)
# Unreal.js Plugin

- core component of [Unreal.js project](https://github.com/ncsoft/Unreal.js)

## getnamo fork

Some opinionated changes to add certain features for live gameplay coding. Not all upstream functionality is guaranteed, but best effort is used when upgrading engine versions.

### Async

Call a function on a background thread and optionally expose communication bridges auto-magically.

[![example.png](https://i.imgur.com/ozenYI8.png)](https://twitter.com/getnamo/status/1276977110762979328)

Format is 
```Async.Lambda( capture params, function, result callback );```

See https://github.com/getnamo/UnrealJs/blob/master/Content/Scripts/async.js for detailed API.

### JavascriptInstance

A javascript component with more fine-grained control over features exposed to js. Can also re-use contexts and isolates.

[![exposure.png](https://i.imgur.com/cZsjeRn.png)](https://twitter.com/getnamo/status/1271772156611817472)

See https://github.com/getnamo/UnrealJs/blob/master/Source/V8/Public/JavascriptInstanceComponent.h for detailed API.


### UModule Package Manager

Should enable 'require' like include with code and asset support and simple external package management for modular development.

WIP: https://github.com/getnamo/UnrealJs/tree/feature-umodule
