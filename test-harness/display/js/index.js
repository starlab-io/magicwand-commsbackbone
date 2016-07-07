// Now we want to load some metadata
(function (){

    // store our query parameters
    var query = window.location.search.replace("?", "");
    var params = query.split("&");
    var paramDict = {};
    params.forEach(function (param) {
        var bits = param.split("=");
        paramDict[bits[0]] = bits[1];
    });

    window.application = {query: paramDict};


    // try to get our history file
    $.getJSON("/data/history.json", function (data) {

        var found = false;

        // now find our archive
        data.archive.forEach(
            function (historic) {
                if (historic.archiveName === window.application.query.test) {
                    window.application.archiveRecord = historic;
                    found = true;
                }
            }
        );

        // fix up our display
        if (found) {

            console.log("Building UI updates");
            console.log(window.application.archiveRecord);

            // data downloads
            Object.keys(window.application.archiveRecord.files).forEach(
                function (source) {
                    window.application.archiveRecord.files[source].forEach(
                        function (file) {
                            // get the file extension
                            var bits = file.split(".");
                            var ext = bits[bits.length - 1];

                            // trim the path to remove the leading "display/"
                            var link = file.replace("display", "", 1);

                            // create the table entry
                            $("#test-data-download").append(
                                "<tr><td><a href='" + link + "'>" + source + " - " + ext + "</a></td></tr>"
                            )

                        }
                    )
                }
            )

            // metadata
            Object.keys(window.application.archiveRecord.metadata).forEach(
                function (metadata_key) {
                    $("#test-metadata").append(
                        "<tr><td>" + metadata_key + "</td><td>" + window.application.archiveRecord.metadata[metadata_key] + "</td></tr>"
                    );

                }
            );

            // configuration data
            var conf_data = {
                "Test Duration": window.application.archiveRecord.testDuration,
                "Archived At": window.application.archiveRecord.addedAt
            };

            Object.keys(conf_data).forEach(
                function (conf_key) {
                    $("#test-config").append(
                        "<tr><td>" + conf_key + "</td><td>" + conf_data[conf_key] + "</td></tr>"
                    );
                }
            );

            // test message contents
            $("#test-message").html(window.application.archiveRecord.message);
        }
    });
})(this);