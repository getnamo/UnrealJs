//Js wrapper to enable raw function passing

//We should only use one instance to handle all lambda logic
Async.instance = Async.StaticInstance();
Async.instance.Callbacks = {};
Async.DevLog = console.log;

class CallbackHandler {
	constructor(lambdaId){
		this.return = ()=>{};
		this.bridges = {};
		this.lambdaId = lambdaId? lambdaId: -1;
		this.pinned = false;

		this.onMessage = (event, data)=>{};
		this.messageCallbacks = {};
		this.callbackId = 0;
	}

	//Todo: hide addBridge/setReturn or split class
	addBridge(name, bridgeFunction){
		this.bridges[name] = (args, resultId)=>{
			Async.DevLog(`Received BT call ${name}, with ${args}`);
			
			//Async.DevLog(bridgeFunction.toString());
			const result = bridgeFunction(JSON.parse(args));

			Async.DevLog(`GT result: ${result}, converting this for callback`);

			if(result != undefined){
				//callback result to BT without expecting receipt
				Async.instance.CallScriptFunction(this.lambdaId, `_GTCallable.Callbacks['${name}']`, JSON.stringify(result), -1);
			}
		}

		//Wrap the necessary js callback function
		return `const ${name} = (_GTArgs, _callback)=>{` +
				`_GTCallable.Callbacks['${name}'] = _callback;\n` + 
				`_GTCallable.CallFunction('${name}', JSON.stringify(_GTArgs), ${this.lambdaId});` + 
				`}\n`;
	}
	setReturn(returnCallback){
		this.return = (jsonValue)=>{
			returnCallback(JSON.parse(jsonValue));
		};
	}

	//function used to run remote thread function
	call(functionName, args, callback){

		let localCallbackId = 0;
		if(callback){
			this.callbackId++;
			this.messageCallbacks[this.callbackId] = callback;
			localCallbackId = this.callbackId;
		}

		Async.instance.CallScriptFunction(this.lambdaId, 'exports.' + functionName, JSON.stringify(args), localCallbackId);
	}

	stop(){
		Async.instance.StopLambda(this.lambdaId);

		//todo: cleanup handler data
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

Async.instance.OnAsyncCall = (name, args, lambdaId) => {
	const handler = Async.instance.Callbacks[lambdaId];

	if(handler != undefined){
		if(handler.bridges[name] != undefined){
			handler.bridges[name](args);
		}
	}
};

//console.log(JSON.stringify(Async.instance))

Async.Lambda = (capture, rawFunction, callback)=>{
	let captureString = "";
	let handler = new CallbackHandler(Async.instance.NextLambdaId());

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
				Async.DevLog(`found function ${key}`);
				captureString += handler.addBridge(key, capture[key]);

				//captureString += 'console.log("Global: " + JSON.stringify(globalThis));\n';
				didFindFunctions = true;	//pinning should only happen if we export?
			}
			else{
				captureString += `let ${key} = JSON.parse(${JSON.stringify(capture[key])});\n`;
			}
		}

		//stringification and parsing will end up with the object value of the capture
		//captureString = "let capture = JSON.parse('"+ JSON.stringify(capture) + "');\n";
	}
	
	//function JSON stringifies any result
	const wrappedFunctionString = '\nJSON.stringify(('+ rawFunction.toString() + ')());';
	const finalScript = "var exports = {}; {\n" + captureString + wrappedFunctionString + '\n}';

	//look for exported functions (rough method)
	if(finalScript.includes('exports.')){
		didFindFunctions = true;
		//Async.DevLog('found functions');
	}
	handler.pinned = didFindFunctions;
	
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