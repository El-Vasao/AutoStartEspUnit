// device OTA update store — stream OTA completes on /upload (reboots; no /ota/start).
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.registerDeviceOtaUpdateStore = function registerDeviceOtaUpdateStore(Alpine) {
    Alpine.store('deviceOta', {
      started: false,
      uploading: false,
      progress: 0,
      status: '',
      file: null,
      setFile(file) { this.file = file; },
      clearLock() {
        this.started = false;
        this.uploading = false;
      },
      clearAfterReboot() {
        this.started = false;
        this.uploading = false;
        this.progress = 0;
        this.status = '';
        this.file = null;
      },
      async start() {
        if (this.started) return;
        if (!this.file) { Alpine.store('uiToast').flash('Выберите файл .bin', 'warning', 4000); return; }
        Alpine.store('uiToast').clearHardwareError();
        this.started = true;
        this.uploading = true;
        this.progress = 0;
        this.status = 'Подготовка…';

        APP.utils.LS.set('otaPostRebootGoPanel', '1');
        APP.utils.LS.set('otaInProgress', '1');
        const formData = new FormData();
        formData.append('data', this.file);
        const xhr = new XMLHttpRequest();
        xhr.open('POST', (APP.api.endpoints?.upload || '/upload'), true);
        xhr.timeout = 180000;
        xhr.upload.onprogress = (e) => {
          if (e.lengthComputable) {
            this.progress = (e.loaded / e.total * 100).toFixed(1);
            this.status = 'Загрузка…';
          }
        };
        xhr.onload = () => {
          if (xhr.status === 200) {
            this.progress = 100;
            this.uploading = false;
            this.status = 'Готово. Ожидание перезагрузки…';
            // Stream OTA flashes during upload; device reboots after final.
          } else {
            APP.utils.LS.del('otaInProgress');
            APP.utils.LS.del('otaPostRebootGoPanel');
            Alpine.store('uiToast').setHardwareError('Ошибка прошивки.');
            this.status = 'Ошибка загрузки';
            this.clearLock();
          }
        };
        xhr.onerror = () => {
          // After HTTP 200 the device may drop Wi‑Fi while rebooting — keep waiting for clocks.
          if (Number(this.progress) >= 100) return;
          APP.utils.LS.del('otaInProgress');
          APP.utils.LS.del('otaPostRebootGoPanel');
          Alpine.store('uiToast').setHardwareError('Ошибка сети при прошивке.');
          this.status = 'Ошибка сети';
          this.clearLock();
        };
        xhr.ontimeout = () => {
          APP.utils.LS.del('otaInProgress');
          APP.utils.LS.del('otaPostRebootGoPanel');
          Alpine.store('uiToast').setHardwareError('Таймаут прошивки.');
          this.status = 'Таймаут загрузки';
          this.clearLock();
        };
        xhr.send(formData);
      }
    });
  };
})();
