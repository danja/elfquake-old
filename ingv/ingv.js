// service dates are UTC, though it shouldn't matter here

var startDate = new Date("2010-01-01T00:00:00Z");
var endDate = new Date("2010-01-10T00:00:00Z");
 //  (1000*60*60*24)

var startWindow = startDate;

while(startWindow < endDate){
  // crop to 2010-01-01T00:00:00
   var startString = startDate.toISOString().substring(0,19);

   var newDate = startWindow.setHours(startWindow.getHours() + 6);
   startDate = new Date(newDate);

   var endString = startDate.toISOString().substring(0,19);
   console.log(startString+"   "+endString);
}

// http://webservices.ingv.it/fdsnws/event/1/query?starttime=2010-01-01T00:00:00&endtime=2010-01-01T06:00:00

// 40N-47N
// 7E-15E
