

class WasmVideoEncoder {
    worker = null;
    onchunk = null;
    onerror = null;
    readyPromise = null;
    isAwaitingFile = false
  
    ackMessageResolvers = {};
  
    constructor(callbacks) {
      this.onchunk = callbacks.output;
      this.onerror = callbacks.error;
      this.ackMessageResolvers = {};
    }
  
    workerMessageReceived = (msg) => {
      const { data } = msg;
       if (this.isAwaitingFile) {
        this.isAwaitingFile = false;
        console.log('Received a file', data);
        const blob = new Blob([data], { type: 'video/mp4' });
        saveFile(blob, 'video.mp4');
        return;
      }
  
      if (data && this.ackMessageResolvers[data.action]) {
        this.ackMessageResolvers[data.action]();
        this.ackMessageResolvers[data.action] = undefined;
        return;
      }
  
      switch(data.action) {
        case 'ready': {
          const chunkMock = {
            type: '',
            timestamp: 0,
            duration: 0,
            data: new Uint8Array()
          }
          this.onchunk(chunkMock);
          break;
        }
        case 'closed': {
          this.isAwaitingFile = true;
          break;
        }
      } 
    }
  
    _loadWorker() {
      this.worker = new Worker('static/ffmpeg/encoder.js');
      this.worker.onerror = (e) => {
        console.log('Failed to load encoder worker: ', e);
        reject(e)
      }
      this.worker.onmessage = this.workerMessageReceived;
    }
  
    async registerMessageAwaiter(messageName) {
      const promiseResolver = (resolve, reject) => {
        this.ackMessageResolvers[messageName] = resolve;
      };
      const promise = new Promise(promiseResolver);
      return promise;
    }
  
    async postAndWait(action, data) {
      const promise = this.registerMessageAwaiter(action);
      this.worker.postMessage({ action, data, returnActionName: action });
      return promise;
    }
  
    configure(config) {
      console.log('Configuring with: ', config)
      this.readyPromise = new Promise(async (resolve, reject) => {
        this._loadWorker();
        await this.postAndWait('encoder_load', 'sequential');
        await this.postAndWait('encoder_init',  {
            videoConfig: {
              width: config.width,
              height: config.height,
              bitrate: config.bitrate,
              fps: config.framerate,
              preset: 0, // ultrafast
            }
        });
        resolve();
      });
    }
  
    encode = (frame) => {
      this.worker.postMessage({ action: 'video' });
      const frameData = new Uint8Array(frame._data).slice();
      this.worker.postMessage(frameData, [frameData.buffer]);
    }
  
    close() {
      this.worker.postMessage({
        action: 'close',
        shouldReturnFile: true,
      });
    }
  
    async flush() {
      // todo
    }
  }
  