//Js wrapper to enable raw function passing
Async.Lambda = (rawFunction, callback)=>{
	const wrappedFunction = '_lambda='+ rawFunction.toString() + ';_lambda()';
	Async.RunScript(wrappedFunction, 'ThreadPool');
}

console.log('async.js complete');