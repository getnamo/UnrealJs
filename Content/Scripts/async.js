//Js wrapper to enable raw function passing

//We should only use one instance to handle all lambda logic
Async.instance = Async.StaticInstance();
Async.instance.Callbacks = {};
Async.DevLog = console.log;

Async.instance.OnLambdaComplete = (returnValue, lambdaId) => {
	//Async.DevLog(returnValue);
	const callback = Async.instance.Callbacks[lambdaId];

	if(callback != undefined){
		Async.instance.Callbacks[lambdaId](returnValue);
		delete Async.instance.Callbacks[lambdaId];
	}
}

//console.log(JSON.stringify(Async.instance))

Async.Lambda = (rawFunction, callback)=>{

	const wrappedFunction = '_lambda='+ rawFunction.toString() + ';_lambda()';
	const lambdaId = Async.instance.RunScript(wrappedFunction, 'ThreadPool');

	//Async.DevLog('id: ' + lambdaId);

	Async.instance.Callbacks[lambdaId] = callback;
}

Async.RunScript = (scriptText, executionContext) => {
	executionContext = executionContext ? executionContext : 'ThreadPool';

	Async.instance.RunScript(scriptText, executionContext);
};

console.log('async.js loaded');