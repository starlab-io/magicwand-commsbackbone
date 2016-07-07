
(function () {
// Apache-pref CPU utilization

var parseDate = d3.timeParse("%Y-%m-%dT%H:%M:%SZ");
var margin = {top: 20, right: 20, bottom: 30, left: 50},
    width = 960 - margin.left - margin.right,
    height = 200 - margin.top - margin.bottom;


var svg = d3.select("#apacheperf-cpu-svg").append("svg")
    .attr("width", width + margin.left + margin.right)
    .attr("height", height + margin.top + margin.bottom);


var x = d3.scaleTime().range([0, width]),
    y = d3.scaleLinear().range([height, 0]),
    z = d3.scaleOrdinal(d3.schemeCategory10);

var stack = d3.stack();

var area = d3.area()
    .x(function(d, i) { return x(d.data.date); })
    .y0(function(d) { return y(d[0]); })
    .y1(function(d) { return y(d[1]); });

var g = svg.append("g")
    .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

d3.csv("/data/apacheperf/" + window.application.query.test + ".csv", type, function(error, data) {
  if (error) throw error;

  var keys = data.columns.filter( (e) => e.startsWith("cpu"));
    console.log(keys);
  x.domain(d3.extent(data, function(d) { return d.date; }));
  z.domain(keys);
  stack.keys(keys);

  var layer = g.selectAll(".layer")
      .data(stack(data))
    .enter().append("g")
      .attr("class", "layer");

  layer.append("path")
      .attr("class", "area")
      .style("fill", function(d) { return z(d.key); })
      .attr("d", area);

  layer.filter(function(d) { return d[d.length - 1][1] - d[d.length - 1][0] > 0.01; })
    .append("text")
      .attr("x", width - 6)
      .attr("y", function(d) { return y((d[d.length - 1][0] + d[d.length - 1][1]) / 2); })
      .attr("dy", ".35em")
      .style("font", "13px sans-serif")
      .style("text-anchor", "end")
      .text(function(d) { return d.key; });

  g.append("g")
      .attr("class", "axis axis--x")
      .attr("transform", "translate(0," + height + ")")
      .call(d3.axisBottom(x));

  g.append("g")
      .attr("class", "axis axis--y")
      .call(d3.axisLeft(y).ticks(10, "%"));
});

function type(d, i, columns) {

var e = {};
  e.date = parseDate(d.timestamp);
  for (var i = 1, n = columns.length; i < n; ++i) {
     if (columns[i].startsWith("cpu")) {
        e[columns[i]] = d[columns[i]] / 100.0;
     }
  }
  return e;
}

})(this);