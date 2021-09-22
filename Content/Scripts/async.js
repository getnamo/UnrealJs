//Js wrapper to enable raw function passing

/*
NB: PoC, the wrapping logic is not currently secure. The instance is directly exposed.
Todo: securely wrap each js module to keep private parts fairly private. 
*/
//We should only use one instance to handle all lambda logic
Async.instance = Async.StaticInstance();
Async.instance.Callbacks = {};
Async.DevLog = console.log;

Async.TypeObj = (obj, showFullClass) => {

	// get toPrototypeString() of obj (handles all types)
	if (showFullClass && typeof obj === "object") {
		return Object.prototype.toString.call(obj);
	}
	if (obj == null) { return (obj + '').toLowerCase(); } // implicit toString() conversion

	var deepType = Object.prototype.toString.call(obj).slice(8,-1).toLowerCase();
	if (deepType === 'generatorfunction') { return 'function' }

	// Prevent overspecificity (for example, [object HTMLDivElement], etc).
	// Account for functionish Regexp (Android <=2.3), functionish <object> element (Chrome <=57, Firefox <=52), etc.
	// String.prototype.match is universally supported.

	return deepType.match(/^(array|bigint|date|error|function|generator|regexp|symbol)$/) ? deepType :
	   (typeof obj === 'object' || typeof obj === 'function') ? 'object' : typeof obj;
}

//Safe way of handling json parsing
Async.ParseArgs = (args)=>{
    try {
        return JSON.parse(args);
    } catch (e) {
    	//arrays may get passed straight through and identify as objects
    	if(typeof(args) === 'object'){
    		return args;
    	}
    	//console.log(e)

    	if(typeof(args) === 'string'){
    		return args;
    	}
    	else{
        	return null;
    	}
    }
}

class CallbackHandler {
	constructor(lambdaId){
		this.return = ()=>{};
		this.bridges = {};
		this.lambdaId = lambdaId? lambdaId: -1;
		this.pinned = false;

		this.onMessage = (event, data)=>{};
		this.messageCallbacks = {};
		this.callbackId = 0;
		this.exports = {};	//todo potentially: support transparent export links so we could do lambda.exports.functionName();
	}

	//called per exposed GT function
	_addBridge(name, bridgeFunction){

		//Wrap the necessary js callback function, this will actually call the bridge function in OnAsyncCall
		let exposeDefinition = 
				`const ${name} = (_GTArgs, _resultCallback)=>{\n` +
				`_asyncUtil.CallbackIndex++;\n` +
				`_asyncUtil.Callbacks['${name}-'+_asyncUtil.CallbackIndex] = _resultCallback;\n` +
				//`if(_resultCallback != undefined){ _asyncUtil.Callbacks['${name}-'+_asyncUtil.CallbackIndex] = _resultCallback;}\n` + 
				`_asyncUtil.CallFunction('${name}', _GTArgs ? JSON.stringify(_GTArgs) : '', ${this.lambdaId}, _asyncUtil.CallbackIndex);\n` + 
				`}\n`;

		//store the GT bridge function with a parse and result callback wrapper. OnAsyncCall call this bridge
		this.bridges[name] = (args, callbackId)=>{
			//parse args if necessary
			args = Async.ParseArgs(args);

			//run bridge function
			const result = bridgeFunction(args);

			//Async.DevLog(`Received BT call ${name}, with ${args}`);
			//Async.DevLog(bridgeFunction.toString());

			//did the function produce a result?
			if(result != undefined){
				//callback to BT with result without expecting receipt (-1)
				Async.instance.CallScriptFunction(this.lambdaId, `_asyncUtil.Callbacks['${name}-${callbackId}']`, JSON.stringify(result), -1);
			}

			//cleanup callback
			Async.instance.RunScriptInLambda(this.lambdaId, `if (_asyncUtil.Callbacks['${name}-${callbackId}']) {delete _asyncUtil.Callbacks['${name}-${callbackId}']};`);
		}
		
		return exposeDefinition;
	}

	//lambda return callback
	_setReturn(returnCallback){
		this.return = (jsonValue)=>{
			returnCallback(jsonValue, this);
		};
	}

	//PUBLIC API: Function used to run remote thread function from GT
	call(functionName, args, callback){
		//no param case
		if(typeof(args) === 'function'){
			callback = args;
			args = null;
		}

		let localCallbackId = 0;
		if(callback){
			this.callbackId++;
			this.messageCallbacks[this.callbackId] = callback;
			localCallbackId = this.callbackId;
		}

		Async.instance.CallScriptFunction(this.lambdaId, 'exports.' + functionName, JSON.stringify(args), localCallbackId);
	}

	//PUBLIC API: stop the lambda and cleanup
	stop(){
		console.log('lambda stop received.');
		
		Async.instance.StopLambda(this.lambdaId);

		//todo: cleanup handler data
		delete Async.instance.Callbacks[this.lambdaId];
	}

	//PUBLIC API: expose uobject
	exposeAs(object, name){
		Async.instance.ExposeObjectInLambda(this.lambdaId, object, name);
	}
};

Async.instance.OnLambdaComplete = (resultString, lambdaId, callbackId /*not used for completion*/) => {
	const result = Async.ParseArgs(resultString);
	const handler = Async.instance.Callbacks[lambdaId];

	//Async.DevLog(`OnLambdaComplete: ${resultString}, ${lambdaId}, ${callbackId}, ${JSON.stringify(handler)}`)

	if(handler != undefined){
		//first: fill the returned exports
		if(handler.pinned && handler.return){
			result.exports.forEach(functionName => {
				handler.exports[functionName] = (args, callback)=>{
					handler.call(functionName, args, callback);
				}
			});
		}
		
		//call the return callback
		try{
			handler.return(result.result);
		}
		catch(e){
			Async.DevLog(e.stack);	
		}

		//cleanup if not pinned
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

//Call from BT to GT functions
Async.instance.OnAsyncCall = (name, args, lambdaId, callbackId) => {
	const handler = Async.instance.Callbacks[lambdaId];

	if(handler != undefined){
		if(handler.bridges[name] != undefined){
			//Async.DevLog(`name: ${name} args:${args} id: ${lambdaId}`);
			//Async.DevLog(handler.bridges[name].toString());
			handler.bridges[name](args, callbackId);
		}
	}
};

//console.log(JSON.stringify(Async.instance))

Async.Lambda = (capture, rawFunction, callback, {enableRequire=false}={})=>{
	let captureString = "";
	let handler = new CallbackHandler(Async.instance.NextLambdaId());

	let didFindFunctions = false;	//affects whether we want to pin the lambda after run, 
									//or if it's disposable

	Async.instance.ClearPreExposures();

	if(typeof(capture) === 'function'){
		//we don't have captures, it's expecting (function, callback)
		callback = rawFunction;
		rawFunction = capture;
	}
	else{
		//find all function passes
		for(let key in capture) {
			const keyType = typeof(capture[key]);

			if(keyType === 'function'){
				//Async.DevLog(`found function ${key}`);
				captureString += handler._addBridge(key, capture[key]);

				//captureString += 'console.log("Global: " + JSON.stringify(globalThis));\n';
				didFindFunctions = true;	//pinning should only happen if we export?
			}
			else if (capture[key].GetObjectClass !== undefined){
				//console.log('this one right here');
				Async.instance.PreExposeObject(capture[key], key);

				console.log('pre exposed: ' + key);
			}
			else{
				captureString += `let ${key} = _asyncUtil.parseArgs(${JSON.stringify(capture[key])});\n`;
			}
		}

		//stringification and parsing will end up with the object value of the capture
		//captureString = "let capture = JSON.parse('"+ JSON.stringify(capture) + "');\n";
	}
	
	//function JSON stringifies any result
	const wrappedFunctionString = `\nJSON.stringify({
		result: lambda(), 
		exports: Object.getOwnPropertyNames(exports)
	});\n`;

	//try{ lambda() } catch(e){ console.log('caught something'); }
	const finalScript = `\nglobalThis.exports = {}; \nglobalThis.lambda = undefined; \n{\n` + 
						`\t_asyncUtil.parseArgs = ${Async.ParseArgs.toString()}\n` + 
						`\tlambda = ${rawFunction.toString()};\n` + 
						`\t` + captureString + 
						`\t` + wrappedFunctionString + 
						'\n}';


	//look for exported functions (rough method)
	if(finalScript.includes('exports.')){
		didFindFunctions = true;
		//Async.DevLog('found functions');
	}
	handler.pinned = didFindFunctions;
	
	//Debug log final script
	Async.DevLog(`Script: <${finalScript}>, found functions: ${didFindFunctions}`);
	const lambdaId = Async.instance.RunScript(finalScript, 'ThreadPool', didFindFunctions, enableRequire);

	handler.lambdaId = lambdaId;
	handler._setReturn(callback);

	//Async.DevLog(`handler: ${JSON.stringify(handler)}`);

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