// Shared WebSocket plumbing for the simulator and the dashboard.
//
// Plain JavaScript on purpose: these pages are opened from a laptop in a forest
// with no internet, and a build step is one more thing to go wrong at camp.

const sg = {
  // Opens the engine's socket and routes frames by their "type".
  //
  //   sg.connect({
  //     onOpen: () => {},
  //     onClose: () => {},
  //     state: (frame) => {},
  //     led: (frame) => {},
  //   })
  //
  // A frame whose type has no handler is ignored. The socket reconnects on its
  // own: the engine is restarted far more often than the pages are.
  connect(handlers) {
    const url = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws';
    let socket = null;
    let retry = 500;

    const open = () => {
      socket = new WebSocket(url);

      socket.onopen = () => {
        retry = 500;
        if (handlers.onOpen) {
          handlers.onOpen();
        }
      };

      socket.onclose = () => {
        if (handlers.onClose) {
          handlers.onClose();
        }
        // Back off up to four seconds, so a closed laptop does not hammer the
        // Pi when it wakes up.
        setTimeout(open, retry);
        retry = Math.min(retry * 2, 4000);
      };

      socket.onmessage = (message) => {
        let frame;
        try {
          frame = JSON.parse(message.data);
        } catch (error) {
          return;
        }
        const handler = handlers[frame.type];
        if (handler) {
          handler(frame);
        }
      };
    };

    open();

    return {
      send(frame) {
        if (socket && socket.readyState === WebSocket.OPEN) {
          socket.send(JSON.stringify(frame));
          return true;
        }
        return false;
      },
    };
  },

  // [255, 120, 0] as a CSS colour.
  css(rgb) {
    if (!rgb || rgb.length !== 3) {
      return '#000000';
    }
    return 'rgb(' + rgb[0] + ',' + rgb[1] + ',' + rgb[2] + ')';
  },

  // Milliseconds of game time as m:ss, which is how a chief reads a countdown.
  clock(millis) {
    const total = Math.max(0, Math.floor(millis / 1000));
    const minutes = Math.floor(total / 60);
    const seconds = total % 60;
    return minutes + ':' + String(seconds).padStart(2, '0');
  },

  // Text into a DOM node, never innerHTML: a team name comes from a config file
  // a chief edited, and it is not markup.
  text(node, value) {
    node.textContent = value === null || value === undefined ? '' : String(value);
    return node;
  },
};
