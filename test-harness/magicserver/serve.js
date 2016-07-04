// Load the net module to create a tcp server.
var net = require('net');
var http = require('http');


var TCP_PORT=5950;
var HTTP_PORT=5980;

function repeatChar(ch, count) {
    if (count == 0) {
        return "";
    }
    var count2 = count / 2;
    var result = ch;

    // double the input until it is long enough.
    while (result.length <= count2) {
        result += result;
    }
    // use substring to hit the precise length target without
    // using extra memory
    return result + result.substring(0, count - result.length);
};

//----------------------------------------------------------------------
//-------- TCP (stream) Server
//----------------------------------------------------------------------

var tcpServer = net.createServer(function (socket) {

  socket.name = socket.remoteAddress + ":" + socket.remotePort 
  
  var streamRequest; //the properties of the stream requested (bitrate etc)
  var running; //the setInterval obj

  socket.on('data', function (data) {
    console.log("> " + data);

    //turn received JSON data into normal object
    streamRequest = JSON.parse(data.toString());
    
    //interrupt previous stream, if any
    clearInterval(running);

    //start new stream based on new streamRequest
    running = setInterval(function(){
      //send a string of 'x' chars  
      socket.write(repeatChar('x',streamRequest.size));
    }, 1000/streamRequest.frequency);
    
    //set current stream to end in time specified by streamRequest
    setTimeout(clearInterval,streamRequest.time,running);
  });

  socket.on('end', function () {
  	//stop streaming when client disconnects
    console.log(socket.name + " disconnected.\n");
    clearInterval(running);
  });

	//"handle" other socket terminations
	socket.on("timeout", function(){
	  clearInterval(running);
	  console.log(socket.name + " timeout.\n");
	});
	socket.on("close", function(){
	  clearInterval(running);
	  console.log(socket.name + " closed.\n");
	});



  console.log("Connection from " + socket.remoteAddress);



  socket.write("HELLO DAVE");
  // tcpServer.destroy(); // kill client after server's response
});

// Fire up the server bound to port 5950
tcpServer.listen(TCP_PORT);
console.log("TCP server listening on port "+ TCP_PORT);

//----------------------------------------------------------------------
//-------- HTTP Server
//----------------------------------------------------------------------

// var httpServer = http.createServer(function (request, response) {
//   response.writeHead(200, {"Content-Type": "text/plain"});
//   response.end("Hello World\n");
// });

// // Listen on port 5980
// httpServer.listen(5980);
// console.log("HTTP server listening on port 5980");