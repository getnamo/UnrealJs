//Js wrapper to enable raw function passing

//We should only use one instance to handle all lambda logic
Async.instance = Async.StaticInstance();
Async.instance.Callbacks = {};
Async.DevLog = console.log;

class CallbackHandler {
	constructor(){
		this.return = ()=>{};
		this.bridges = {};
		this.lambdaId = -1;
		this.pinned = false;

		this.onMessage = (event, data)=>{};
		this.messageCallbacks = {};
		this.callbackId = 0;
	}

	//Todo: hide addBridge/setReturn or split class
	addBridge(name, bridgeFunction){
		this.bridges[name] = ()=>{

			
			//todo: make bridge, need a way to call this function on bg thread work.
			//likely needs a modification in runscript?
			//to call 
		}
	}
	setReturn(returnCallback){
		this.return = (jsonValue)=>{
			returnCallback(JSON.parse(jsonValue));
		};
	}

	//function used to run remote thread function
	call(functionName, args, callback){
		//Todo: remotely execute this function/arg pair

		let localCallbackId = 0;
		if(callback){
			this.callbackId++;
			this.messageCallbacks[this.callbackId] = callback;
			localCallbackId = this.callbackId;
		}

		Async.instance.CallScriptFunction(this.lambdaId, 'exports.' + functionName, JSON.stringify(args), localCallbackId);
	}
};

Async.instance.OnLambdaComplete = (result, lambdaId, callbackId /*not used for completion*/) => {
	const handler = Async.instance.Callbacks[lambdaId];

	if(handler != undefined){
		handler.return(result);
		if(!handler.pinned){
			Async.DevLog('Lambda cleaned up');
			delete Async.instance.Callbacks[lambdaId];
		}
	}
};

Async.instance.OnMessage = (message, lambdaId, callbackId) => {
	//Async.DevLog('Got message: ' + message + ' from ' + lambaId);
	const handler = Async.instance.Callbacks[lambdaId];

	if(handler != undefined){
		if(handler.messageCallbacks[callbackId] != undefined){
			handler.messageCallbacks[callbackId](message);
			delete handler.messageCallbacks[callbackId];
		}
	}
};

//console.log(JSON.stringify(Async.instance))

Async.Lambda = (capture, rawFunction, callback)=>{
	let captureString = "";
	let handler = new CallbackHandler();
	let didFindFunctions = false;	//affects whether we want to pin the lambda after run, 
									//or if it's disposable

	if(typeof(capture) === 'function'){
		//we don't have captures, it's expecting (function, callback)
		callback = rawFunction;
		rawFunction = capture;
	}
	else{
		//find all function passes
		for(let key in capture) {
			if(typeof(capture[key]) === 'function'){
				//Async.DevLog(`found key ${key}`);
				handler.addBridge(key, capture[key]);
				didFindFunctions = true;
			}
		}

		//stringification and parsing will end up with the object value of the capture
		captureString = "let capture = JSON.parse('"+ JSON.stringify(capture) + "');\n";
	}
	handler.pinned = didFindFunctions;
	
	//function JSON stringifies any result
	const wrappedFunctionString = '\nJSON.stringify(('+ rawFunction.toString() + ')());';
	const finalScript = "var exports = {}; {\n" + captureString + wrappedFunctionString + '\n}';
	
	Async.DevLog(finalScript);

	const lambdaId = Async.instance.RunScript(finalScript, 'ThreadPool', didFindFunctions);

	handler.lambdaId = lambdaId;
	handler.setReturn(callback);

	Async.DevLog(`handler: ${JSON.stringify(handler)}`);

	//wrap the callback with a JSON.parse so the return value is in object form
	Async.instance.Callbacks[lambdaId] = handler;

	//return a bridge callback
	return handler;
};

Async.RunScript = (scriptText, executionContext) => {
	executionContext = executionContext ? executionContext : 'ThreadPool';

	Async.instance.RunScript(scriptText, executionContext);
};

console.log('async.js loaded');