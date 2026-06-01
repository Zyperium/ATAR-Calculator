import re
import requests

downloads_urls = [
    "https://vtac.edu.au/files/pdf/reports/scaling-report-25.pdf", 
    "https://vtac.edu.au/files/pdf/reports/scaling-report-24.pdf", 
    "https://vtac.edu.au/files/pdf/reports/scaling-report-23-24.pdf", 
    "https://vtac.edu.au/files/pdf/reports/scaling-report-22-23.pdf",
    "https://vtac.edu.au/files/pdf/reports/scaling-report-21-22.pdf", 
    "https://vtac.edu.au/files/pdf/reports/scaling-report-20-21.pdf",
    "https://vtac.edu.au/files/pdf/reports/scaling_report_19.pdf", 
    "https://vtac.edu.au/files/pdf/scaling_report_18.pdf",
    "https://vtac.edu.au/files/pdf/scaling_report_17.pdf", 
    "https://vtac.edu.au/files/pdf/scaling_report_16.pdf",
    "https://vtac.edu.au/files/pdf/scaling_report_15.pdf"
]

def extract_year(url):
    match = re.search(r'[-_](\d+(?:-\d+)?)\.pdf$', url)
    if match:
        raw_year = match.group(1)
        start_year = raw_year.split('-')[0]
        return f"20{start_year}"
    return "unknown"

def main():
    print("Starting PDF downloads into CWD...")
    for url in downloads_urls:
        year = extract_year(url)
        filename = f"scaling_{year}.pdf"
        
        try:
            with requests.get(url, stream=True) as r:
                r.raise_for_status()
                with open(filename, "wb") as f:
                    for chunk in r.iter_content(chunk_size=8192):
                        f.write(chunk)
            print(f"✓ Downloaded: {filename}")
        except Exception as e:
            print(f"✗ Failed {url}: {e}")

if __name__ == "__main__":
    main()