"use strict"

function makeWindowTimer(target) {
    let freeSymbols = []
    function sym_alloc() {
        if (freeSymbols.length) {
            return freeSymbols.pop()
        } else {
            return Symbol()
        }
    }
    function sym_free(sym) {
        freeSymbols.push(sym)
    }

    let FastPriorityQueue = require('./FastPriorityQueue')
    let Queue = new FastPriorityQueue(function (a, b) {
        return a.T < b.T;
    })
    let currentTime = 0
    let frame = 0
    let timers = {}    
    target.setTimeout = function (handler, timeout) {
        let timerId = sym_alloc()
        let t = timers[timerId] = {
            T: currentTime + timeout,
            handler: handler,
            frame: frame,
            active: true,
            interval: 0
        }
        Queue.add(t)
        return timerId
    }
    target.clearTimeout = function (timerId) {
        let t = timers[timerId]
        if (t) {
            t.active = false
            delete timers[timerId]
            sym_free(timerId)
        }
    }
    target.setInterval = function (handler, interval) {
        let timerId = sym_alloc()
        let t = timers[timerId] = {
            T: currentTime + interval,
            handler: handler,
            frame: frame,
            active: true,
            interval: interval
        }
        Queue.add(t)
        return timerId
    }
    target.clearInterval = target.clearTimeout

    return function (elapsedTime) {
        currentTime += elapsedTime
        while (Queue.size) {
            let y = Queue.peek()
            if (y.T >= currentTime || y.frame == frame) break

            Queue.poll()
            if (y.active) {
                y.handler()                
                if (y.interval) {
                    y.T += y.interval
                    Queue.add(y)
                }
            }
        }
        frame++
    }
}

(function (target) {
    if (Root == undefined || Root.OnTick == undefined) return
    
    let timerLoop = makeWindowTimer(target);

    let current_time = 0
    target.$time = 0

    let nextTicks = []

    function flushTicks() {
        nextTicks.push(null)
        while (true) {
            let x = nextTicks.shift()
            if (!x) break
            x()
        }
    }    
    
    let root = function (elapsedTime) {        
        flushTicks()
        
        current_time += elapsedTime
        target.$time = current_time
        
        timerLoop((elapsedTime * 1000) | 0)
    }

    target.process = {
        nextTick: function (fn) {
            nextTicks.push(fn);
            //console.log('added timer: <' + fn + '>');
        },        
        argv: [],
        argc: 0,
        platform: 'UnrealJS',
        env: {}
    }



    Root.OnTick.Add(root)
})(global)