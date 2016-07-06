
// HTTPerf reply-rate graph
(function () {
var margin = {top: 20, right: 20, bottom: 30, left: 50},
    width = 960 - margin.left - margin.right,
    height = 400 - margin.top - margin.bottom;

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
    .x(function(d) { return x(d.date); })
    .y(function(d) { return y(d.rate); });

var httperfSvg = d3.select("#httperf-svg").append("svg")
    .attr("width", width + margin.left + margin.right)
    .attr("height", height + margin.top + margin.bottom)
  .append("g")
    .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

d3.csv("/data/httperf/performance.csv", httperfType, function(error, data) {
  if (error) throw error;

  x.domain(d3.extent(data, function(d) { return d.date; }));
  y.domain(d3.extent(data, function(d) { return d.rate; }));

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
      .datum(data)
      .attr("class", "line")
      .attr("d", line);
});

function httperfType(d) {
  d.date = formatDate(d["mm:ss elapsed"]);
  d.rate = +d["reply rate"];

  return d;
}

})(this);