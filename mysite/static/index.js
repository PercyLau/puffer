function rand_alg() {
  var rand_urls = [
    "/player.html?aid=1",
    "/player.html?aid=2",
    "/player.html?aid=3",
    "/player.html?aid=4",
    "/player.html?aid=5",
    "/player.html?aid=6",
    "/player.html?aid=7",
    "/player.html?aid=8",
    "/player.html?aid=10",
    "/player.html?aid=11"
  ];

  var rand_url = rand_urls[Math.floor(Math.random() * rand_urls.length)];
  window.location.replace(rand_url);
}

function init_app() {
  /* Listening for auth state changes */
  firebase.auth().onAuthStateChanged(function(user) {
    if (user) {
      /* User is signed in */
        //Check if account creation time is same as lastsignintime, if so, this is first log in
        //and token handling needs to occur
        //Below variables are UTC strings and need to be converted to be useful
      var newUser = false; //Variable to determine if this is the first time user has visited site
      var timeSinceCreation = Date.parse(user.metadata.lastSignInTime) - Date.parse(user.metadata.creationTime);
      if (timeSinceCreation < 2) {
        newUser = true;
      }

      if (newUser) {
        // Insert code here to check for token, and, if present, save token to database
        // If token is not present, then display warning
      }

      console.log("Hudson: The user is " + newUser + " a new user (timeSinceCreation: " + timeSinceCreation + ")");
      document.getElementById('user-signed-in').style.display = 'block';
      document.getElementById('user-info').textContent = 'Welcome! ' + user.displayName;

      document.getElementById('play-rand-alg').addEventListener('click', rand_alg);
    } else {
      /* Redirect to the sign-in page if user is not signed in */
      window.location.replace('/widget.html');
    }
  });

  document.getElementById('sign-out').addEventListener('click',
    function() {
      firebase.auth().signOut();
    }
  );
}

window.addEventListener('load', init_app);
