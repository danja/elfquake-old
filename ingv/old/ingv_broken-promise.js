/***
 See http://webservices.rm.ingv.it/fdsnws/event/1/
 ***/

var parseString = require('xml2js').parseString;
var fs = require('fs');

// http://webservices.ingv.it/fdsnws/event/1/query?starttime=2010-01-01T00:00:00&endtime=2010-01-01T06:00:00

// 40N-47N
// 7E-15E

const baseUrl = 'http://webservices.ingv.it/fdsnws/event/1/query?';

// service dates are UTC, though it shouldn't matter here
var startDate = new Date("2010-01-01T00:00:00Z");
var endDate = new Date("2010-01-10T00:00:00Z");

const DATA_DIR = "./csv_data/";


/* Dependency-less GET promise from
   https://www.tomas-dvorak.cz/posts/nodejs-request-without-dependencies/ */
const getContent = function(url) {
  // return new pending promise
  return new Promise((resolve, reject) => {
    // select http or https module, depending on reqested url
    const lib = url.startsWith('https') ? require('https') : require('http');
    const request = lib.get(url, (response) => {
      // handle http errors
      if (response.statusCode < 200 || response.statusCode > 299) {
        reject(new Error('Failed to load page, status code: ' + response.statusCode));
      }
      // temporary data holder
      const body = [];
      // on every content chunk, push it to the data array
      response.on('data', (chunk) => body.push(chunk));
      // we are done, resolve promise with those joined chunks
      response.on('end', () => resolve(body.join('')));
    });
    // handle connection errors of the request
    request.on('error', (err) => reject(err))
  })
};

  var startWindow = startDate;

  while (startWindow < endDate) {
    // crop to 2010-01-01T00:00:00
    var startString = startDate.toISOString().substring(0, 19);

    var newDate = startWindow.setHours(startWindow.getHours() + 6);
    startDate = new Date(newDate);

    var endString = startDate.toISOString().substring(0, 19);

    // 'http://webservices.ingv.it/fdsnws/event/1/query?starttime=2010-01-01T00:00:00&endtime=2010-01-01T06:00:00';

    var url = baseUrl + 'starttime=' + startString + '&endtime=' + endString;
    console.log(url);

    var filename = DATA_DIR + startString + ".csv";
    console.log("getting "+filename);
    getContent(url)
      .then((xml) =>
        parseString(xml, function(err, result) { // XML -> JSON
          console.log("processing "+filename);
          process(filename, result);
        })
      )
      .catch((err) => console.error(err));
      console.log('AFTER');
  }

//q:quakeml/eventParameters/event/origin/time/value
//q:quakeml/eventParameters/event/origin/latitude/value
//q:quakeml/eventParameters/event/origin/longitude/value
//q:quakeml/eventParameters/event/origin/depth/value
//q:quakeml/eventParameters/event/magnitude/mag/value

/*
    Pulls values out of JSON
*/
var process = function(filename, data) {
  //  console.log(JSON.stringify(data, null, 2));
  var events = data["q:quakeml"]["eventParameters"][0]["event"]; // ["event"];

  var csv = "";

  for (i in events) {
    var lat = events[i]["origin"][0]["latitude"][0]["value"][0];
    var long = events[i]["origin"][0]["longitude"][0]["value"][0];
    var depth = events[i]["origin"][0]["depth"][0]["value"][0];
    var magnitude = events[i]["magnitude"][0]["mag"][0]["value"][0];
    // console.log(long);
    var eventLat = parseFloat(lat);
    var eventLong = parseFloat(long);
    var eventDepth = parseFloat(depth);
    var eventMagnitude = parseFloat(magnitude);

var current = eventLat + ", " + eventLong + ", " + eventMagnitude + ", " + eventDepth + "\n";
    csv = csv + current;
  //  console.log(eventLat + ", " + eventLong + ", " + eventMagnitude + ", " + eventDepth);
  }

  var buffer = new Buffer(csv); // TODO move up

  fs.open(filename, 'w', function(err, fd) {
      if (err) {
          throw 'error opening file: ' + err;
      }

      fs.write(fd, buffer, 0, buffer.length, null, function(err) {
          if (err) throw 'error writing file: ' + err;
          fs.close(fd, function() {
              console.log('Saved '+filename);
          })
      });
  });
}
