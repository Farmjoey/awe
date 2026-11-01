#!/usr/bin/env node
// Used by .github/workflows/widget-publish.yml: takes the JSON block of an approved issue,
// validates it with the same rules as the board, stores it and refreshes the index.
// usage: node tools/widget_publish.js <issue-body-file> <issue-number>
const fs = require('fs'), path = require('path');
const W = require('../app/widgetlib.js');
const [bodyFile, num] = process.argv.slice(2);
const body = fs.readFileSync(bodyFile, 'utf8');
const m = /```json\s*([\s\S]*?)```/.exec(body);
if (!m) { console.log('NO_JSON'); process.exit(2); }
let w;
try { w = JSON.parse(m[1]); } catch (e) { console.log('BAD_JSON'); process.exit(2); }
const errs = W.validateFile(w);
if (errs.length) { console.log('INVALID: ' + errs.join('; ')); process.exit(3); }
const slug = (w.name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'widget') + '-' + String(num).replace(/\D/g, '');
const dir = path.join(__dirname, '..', 'app', 'widgets', 'community');
fs.mkdirSync(dir, { recursive: true });
const clean = { v: 1, name: w.name, author: w.author, desc: w.desc || '', items: w.items };
fs.writeFileSync(path.join(dir, slug + '.json'), JSON.stringify(clean, null, 1));
const names = fs.readdirSync(dir).filter(f => f.endsWith('.json') && f !== 'index.json').map(f => f.slice(0, -5)).sort();
fs.writeFileSync(path.join(dir, 'index.json'), JSON.stringify({ v: 1, widgets: names }));
console.log('OK ' + slug);
