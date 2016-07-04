
var net = require('net');

function requestStream(time,frequency,size){
  var streamParameters = {time: time, frequency: frequency, size: size};
  var parameterString = JSON.stringify(streamParameters);
  client.write(parameterString);
}

var client = new net.Socket();

console.log("Target IP: " + process.argv[2]);
client.connect(5950, process.argv[2], function() {
// client.connect(5950, '172.17.0.3', function() {
  console.log('Connected');
  requestStream(6000,24,100);
  // var streamParameters = {time: 60, frequency: 24, size: 10000};
  // var parameterString = JSON.stringify(streamParameters);
  // client.write(parameterString);
  setTimeout(requestStream,5000,6000,24,200);
});

client.on('data', function(data) {
  console.log('Received: ' + data.length + " bytes");
  // client.destroy(); // kill client after server's response
});

client.on('close', function() {
  console.log('Connection closed');
});