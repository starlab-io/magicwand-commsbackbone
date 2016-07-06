(function (){

    // try to get our history file
    $.getJSON("/data/history.json", function (data) {
        console.log("Got history data: " );
        $("#archive-size").html(data.archive.length);

        data.archive.forEach(
            function (historic) {
                $("#history-container").append(
                    "<div class='row'><div class='col-md-8 col-md-offset-2'><h2>" + historic["addedAt"] + "</h2><p>" + historic["message"] + "</p></div></div>"
                )
            }
        );
    });
})(this);