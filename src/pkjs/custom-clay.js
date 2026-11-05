// Farbvorlagen fuer die Clay-Seite: die Auswahl unter "Template" setzt alle
// Farbfelder auf einmal. Wer danach eine einzelne Farbe aendert, landet bei
// "Custom" -- die Vorlage ist nur ein Startpunkt.
//
// Clay schickt diese Funktion als Quelltext an die Konfigseite, deshalb muss
// alles, was sie braucht, in ihr selbst stehen (kein require).

module.exports = function() {
  var clayConfig = this;

  // Reihenfolge der Spalten = COLOR_KEYS
  var COLOR_KEYS = ['COLOR_BG', 'COLOR_HOUR', 'COLOR_MINUTE', 'COLOR_TEXT', 'COLOR_ACCENT'];

  //                bg        hour      minute    text      accent
  var TEMPLATES = {
    noir:     ['000000', 'FFFFFF', 'FFFFFF', 'AAAAAA', 'FFFFFF'],
    classic:  ['FFFFFF', '000000', '000000', '555555', '000000'],
    swiss:    ['FFFFFF', '000000', 'FF0000', '000000', 'FF0000'],
    blush:    ['FFAAAA', '000000', 'AA0055', '000000', 'AA0055'],
    berry:    ['AA0055', 'FFFFFF', 'FFAAFF', 'FFFFFF', 'FFAAFF'],
    lavender: ['AAAAFF', '000055', '5500AA', '000055', '5500AA'],
    mint:     ['AAFFAA', '005500', '005555', '005500', '005555'],
    navy:     ['000055', 'FFFFFF', '55AAFF', 'AAAAFF', '55AAFF'],
    ocean:    ['0055AA', 'FFFFFF', 'AAFFFF', 'FFFFFF', 'AAFFFF'],
    forest:   ['005500', 'FFFFFF', 'AAFF55', 'FFFFFF', 'AAFF55'],
    plum:     ['550055', 'FFFFFF', 'FFAAFF', 'FFFFFF', 'FFAAFF'],
    sport:    ['000000', 'FFFFFF', 'FF5500', 'AAAAAA', 'FF5500']
  };

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var template = clayConfig.getItemByMessageKey('TEMPLATE');
    if (!template) { return; }
    var applying = false;

    template.on('change', function() {
      var colors = TEMPLATES[template.get()];
      if (!colors) { return; }
      applying = true;
      for (var i = 0; i < COLOR_KEYS.length; i++) {
        var item = clayConfig.getItemByMessageKey(COLOR_KEYS[i]);
        if (item) { item.set(parseInt(colors[i], 16)); }
      }
      applying = false;
    });

    COLOR_KEYS.forEach(function(key) {
      var item = clayConfig.getItemByMessageKey(key);
      if (!item) { return; }
      item.on('change', function() {
        if (!applying && template.get() !== 'custom') { template.set('custom'); }
      });
    });
  });
};
