function load_script(script_path) {
  /* Create and append a new script */
  var new_script = document.createElement('script');
  new_script.type = 'text/javascript';
  new_script.src = script_path;
  document.getElementsByTagName('head')[0].appendChild(new_script);
  return new_script;
}

function start_dashjs(aid, session_key) {
  const channel_select = document.getElementById('channel-select');
  var manifest_url = '/static/puffer/media/' + channel_select.value + '/ready/live.mpd';

  var player = dashjs.MediaPlayer().create();
  player.initialize(document.getElementById("tv-player"), manifest_url, true);
  player.clearDefaultUTCTimingSources();

  channel_select.onchange = function() {
    console.log('set channel:', channel_select.value);
    player.attachSource('/static/puffer/media/' + channel_select.value + '/ready/live.mpd');
  };

  if (aid === 2) {  // default dash.js
  } else if (aid === 3) {  // BOLA dash.js
    player.setABRStrategy('abrBola');
  } else {
    /* Uncomment this block if you want to change the buffer size
       that dash.js tries to maintain */
    /*
    player.setBufferTimeAtTopQuality(60);
    player.setStableBufferTime(60);
    player.setBufferToKeep(60);
    player.setBufferPruningInterval(60);
    */

    /* algorithm IDs in pensieve:
      1: 'Fixed Rate'
      2: 'Buffer Based'
      3: 'Rate Based'
      4: 'Pensieve'
      5: 'Festive'
      6: (occupied)
      7: 'FastMPC
      8: 'RobustMPC' */
    pensieve_abr_id = aid - 3;

    if (pensieve_abr_id > 1) {
      player.enablerlABR(true);
    }

    player.setAbrAlgorithm(pensieve_abr_id);
  }
}

function setup_control_bar() {
  const video = document.getElementById('tv-player');
  const mute_button = document.getElementById('mute-button');
  const volume_bar = document.getElementById('volume-bar');
  const full_screen_button = document.getElementById('full-screen-button');

  var last_volume_before_mute = video.volume;

  mute_button.onclick = function() {
    if (mute_button.muted == true) {
      video.volume = last_volume_before_mute;
      volume_bar.value = video.volume;
      mute_button.muted = false;
      mute_button.style.backgroundImage = "url(/static/puffer/images/volume_on.svg)";
    } else {
      mute_button.muted = true;
      last_volume_before_mute = video.volume;
      video.volume = 0;
      volume_bar.value = 0;
      mute_button.style.backgroundImage = "url(/static/puffer/images/volume_off.svg)";
    }
  };

  volume_bar.value = video.volume;
  volume_bar.onchange = function() {
    video.volume = volume_bar.value;
    if (video.volume > 0) {
      mute_button.style.backgroundImage = "url(/static/puffer/images/volume_on.svg)";
      mute_button.muted = false;
    } else {
      mute_button.style.backgroundImage = "url(/static/puffer/images/volume_off.svg)";
      mute_button.muted = true;
    }
  };

  full_screen_button.onclick = function() {
    if (video.requestFullscreen) {
      video.requestFullscreen();
    } else if (video.mozRequestFullScreen) {
      video.mozRequestFullScreen();
    } else if (video.webkitRequestFullscreen) {
      video.webkitRequestFullscreen();
    }
  };
}

function init_player(params_json) {
  var params = JSON.parse(params_json);

  var aid = Number(params.aid);
  var session_key = params.session_key;

  /* assert that session_key is not null */
  if (!session_key) {
    console.log('Error: no session key')
    return;
  }

  /* Set up the player control bar */
  setup_control_bar();

  if (aid === 1) {  // puffer
    load_script('/static/puffer/js/puffer.js').onload = function() {
      start_puffer(session_key);  // start_puffer is defined in puffer.js
    }
  } else {
    /* All the other algorithms are based on dash.js */
    var new_script = null;

    if (aid === 2 || aid === 3) {  // algorithms available in dash.js
      new_script = load_script('/static/puffer/dist/dash.all.min.js');
    } else if (aid >= 4 && aid <= 11) {  // algorithms available in pensieve
      new_script = load_script('/static/puffer/dist/pensieve.dash.all.debug.js');
    }

    new_script.onload = function() {
      start_dashjs(aid, session_key);
    }
  }
}
