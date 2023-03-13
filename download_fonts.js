const https = require('https');
const fs = require('fs');

const url = `https://firestore.googleapis.com/v1/projects/plx-clay/databases/(default)/documents/revisions/5/fonts`

const downloadFile = (url) => {
    return new Promise((resolve, reject) => {
        https.get(url, res => {
            let data = [];
            const headerDate = res.headers && res.headers.date ? res.headers.date : 'no response date';
            console.log('Status Code:', res.statusCode);
            console.log('Date in Response header:', headerDate);
    
            res.on('data', chunk => {data.push(chunk);});
    
            res.on('end', () => {
                console.log('Response ended: ');
                resolve(Buffer.concat(data));
            });
        }).on('error', err => {
            console.log('Error: ', err.message);
            reject(err);
        });
    });
   
}

async function run() {
    const data = await downloadFile(url);
    const doc = JSON.parse(data);

    const fontUrl = doc["documents"].map(e => e.fields.downloadUrl.stringValue);
    for (const f of fontUrl) {
        const fileName = f.split('/fonts/')[1];
        const data = await downloadFile(f);
        fs.writeFileSync(`fonts/${fileName}`, data); 
    }   
}


run();