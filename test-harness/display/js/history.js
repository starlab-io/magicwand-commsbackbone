(function (){

    // try to get our history file
    $.getJSON("/data/history.json", function (data) {
        console.log("Got history data: " );
        $("#archive-size").html(data.archive.length);

        data.archive.forEach(
            function (historic) {
                $("#history-table-body").append(
                    "<tr>" +
                    "<td>" + "<a href='/index.html?test=" + historic.archiveName + "'>"+ historic.archiveName + "</a></td>" +
                    "<td>" + historic.addedAt + "</td>" +
                    "<td>" + historic.testDuration + " seconds</td>" +
                    "<td>" + historic.message + "</td>" +
                    "<td>" + $.map(historic.files, (v, k) => k).join(", ") + "</td>"
                )
            }
        );
    });
})(this);