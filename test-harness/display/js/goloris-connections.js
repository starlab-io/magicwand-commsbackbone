
// GoLoris connection graph
(function () {
var margin = {top: 20, right: 20, bottom: 30, left: 50},
    width = 960 - margin.left - margin.right,
    height = 200 - margin.top - margin.bottom;

var formatDate = d3.timeParse("%Y-%m-%dT%H:%M:%SZ");

var x = d3.scaleTime()
    .range([0, width]);

var y = d3.scaleLinear()
    .range([height, 0]);

var xAxis = d3.axisBottom()
    .scale(x);

var yAxis = d3.axisLeft()
    .scale(y);

var line = d3.line()
    .x(function(d) { return x(formatDate(d.timestamp)); })
    .y(function(d) { return y(d.avg); });

var httperfSvg = d3.select("#goloris-connections-svg").append("svg")
    .attr("width", width + margin.left + margin.right)
    .attr("height", height + margin.top + margin.bottom)
  .append("g")
    .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

// we know from the app layer which test we're trying to view
d3.json("/data/goloris/" + window.application.query.test + ".json", function(error, data) {
  if (error) throw error;
console.log("Got goloris data...");
console.log(data);
  x.domain(d3.extent(data.aggregate, function(d) { return formatDate(d.timestamp); }));
  y.domain(d3.extent(data.aggregate, function(d) { return d.avg; }));

  httperfSvg.append("g")
      .attr("class", "x axis")
      .attr("transform", "translate(0," + height + ")")
      .call(xAxis);

  httperfSvg.append("g")
      .attr("class", "y axis")
      .call(yAxis)
    .append("text")
      .attr("transform", "rotate(-90)")
      .attr("y", 6)
      .attr("dy", ".71em")
      .style("text-anchor", "end")
      .text("Reply-Rate");

  httperfSvg.append("path")
      .datum(data.aggregate)
      .attr("class", "line")
      .attr("d", line);
});

})(this);