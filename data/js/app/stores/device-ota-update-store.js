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
      async start() {
        if (this.started) return;
        if (!this.file) { Alpine.store('uiStatusBar').flash('Выберите файл обновления.', 'warning', 4000); return; }
        Alpine.store('uiStatusBar').clearHardwareError();
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
        xhr.upload.onprogress = (e) => {
          if (e.lengthComputable) {
            this.progress = (e.loaded / e.total * 100).toFixed(1);
            this.status = 'Загрузка и прошивка…';
          }
        };
        xhr.onload = () => {
          if (xhr.status === 200) {
            this.progress = 100;
            this.status = 'Обновление принято, ожидайте перезагрузку…';
            // Stream OTA flashes during upload; device reboots after final.
          } else {
            APP.utils.LS.del('otaInProgress');
            Alpine.store('uiStatusBar').setHardwareError('Ошибка загрузки файла обновления. Перезагрузите устройство при необходимости.');
            this.status = 'Ошибка загрузки';
            this.started = false;
            this.uploading = false;
          }
        };
        xhr.onerror = () => {
          APP.utils.LS.del('otaInProgress');
          Alpine.store('uiStatusBar').setHardwareError('Ошибка сети при загрузке OTA. Проверьте соединение.');
          this.status = 'Ошибка сети';
          this.started = false;
          this.uploading = false;
        };
        xhr.send(formData);
      }
    });
  };
})();
