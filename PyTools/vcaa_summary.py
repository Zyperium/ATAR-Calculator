import json
import re
from pathlib import Path
import requests
from bs4 import BeautifulSoup

def fetch_vcaa_html(url):
    headers = {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
    }
    print(f"Fetching VCAA assessment summary from {url}...")
    response = requests.get(url, headers=headers)
    response.raise_for_status()
    return response.text

def parse_study_column(cell_text):
    lines = [line.strip() for line in cell_text.split('\n') if line.strip()]
    category = ""
    subjects = []
    
    for line in lines:
        if line.endswith(':') or (':' in line and not re.search(r'[A-Z]{2}\d{2}', line)):
            category = line.split(':')[0].strip()
            continue
            
        match = re.search(r'([A-Z]{2}\d{2})', line)
        if match:
            code = match.group(1)
            name_part = line.split(code)[0].strip()
        else:
            code = ""
            name_part = line.strip()
            
        name_part = re.sub(r'[\(\s:\-\s]+$', '', name_part).strip()
        if not name_part:
            continue
            
        if category and category.lower() not in name_part.lower():
            full_name = f"{category}: {name_part}"
        else:
            full_name = name_part
                
        subjects.append({"code": code, "name": full_name})
            
    return subjects

def find_best_vtac_key(vcaa_code, vcaa_name, conglomerate_data):
    if not conglomerate_data:
        return vcaa_code if vcaa_code else vcaa_name
    
    if vcaa_code:
        if vcaa_code in conglomerate_data:
            return vcaa_code
        
        for vtac_code, info in conglomerate_data.items():
            if info.get("vcaa_code") == vcaa_code:
                return vtac_code

    def clean(text):
        return re.sub(r'[^a-z0-9]', '', text.lower())
        
    vcaa_clean = clean(vcaa_name)
    
    for vtac_code, info in conglomerate_data.items():
        if clean(info.get("subject", "")) == vcaa_clean:
            return vtac_code
        
    for vtac_code, info in conglomerate_data.items():
        vtac_clean = clean(info.get("subject", ""))
        if vtac_clean and (vtac_clean in vcaa_clean or vcaa_clean in vtac_clean):
            return vtac_code
        
    import difflib
    vtac_names = {info.get("subject", ""): vtac_code for vtac_code, info in conglomerate_data.items() if "subject" in info}
    matches = difflib.get_close_matches(vcaa_name, vtac_names.keys(), n=1, cutoff=0.5)
    if matches:
        return vtac_names[matches[0]]
    
    if vcaa_code:
        for vtac_code in sorted(conglomerate_data.keys(), key=len, reverse=True):
            if vcaa_code.startswith(vtac_code) or vtac_code.startswith(vcaa_code):
                return vtac_code
        
    return vcaa_code if vcaa_code else vcaa_name

def main():
    url = "https://www.vcaa.vic.edu.au/administration/vce-administrative-handbook/vce-and-vet-assessment-summary"
    
    try:
        html_content = fetch_vcaa_html(url)
    except Exception as e:
        print(f"✗ Failed to download VCAA page: {e}")
        return

    soup = BeautifulSoup(html_content, 'html.parser')
    tables = soup.find_all('table')
    
    if not tables:
        print("✗ No data tables discovered on the target VCAA viewport.")
        return
        
    print(f"Found {len(tables)} data structures. Extracting metrics...")
    raw_vcaa_records = []
    
    for table in tables:
        rows = table.find_all('tr')
        for row in rows:
            cells = row.find_all(['td', 'th'])
            if len(cells) < 4:
                continue
                
            col0_text = cells[0].get_text().strip()
            if "Study" in col0_text or "Program" in col0_text:
                continue
                
            subjects = parse_study_column(cells[0].get_text('\n'))
            if not subjects:
                continue
                
            graded_ids = re.findall(r'\d+', cells[1].get_text(' '))
            contributions = re.findall(r'\d+(?:\.\d+)?', cells[3].get_text(' '))
            
            types_text = cells[2].get_text('\n')
            type_lines = [t.strip() for t in types_text.split('\n') if t.strip()]
            
            if len(type_lines) != len(graded_ids) and len(graded_ids) > 0:
                alt_lines = [t.strip() for t in re.split(r'\s{2,}', cells[2].get_text()) if t.strip()]
                if len(alt_lines) == len(graded_ids):
                    type_lines = alt_lines
                    
            assessments = []
            num_assessments = max(len(graded_ids), len(type_lines), len(contributions))
            
            for i in range(num_assessments):
                g_id = graded_ids[i] if i < len(graded_ids) else f"{i+1}"
                a_type = type_lines[i] if i < len(type_lines) else "Unknown Assessment Structural Node"
                contrib = contributions[i] if i < len(contributions) else "0"
                
                a_type = re.sub(r'School[- ]assessed Coursework', 'SAC', a_type, flags=re.IGNORECASE)
                a_type = re.sub(r'School[- ]assessed Task', 'SAT', a_type, flags=re.IGNORECASE)
                
                try:
                    contrib = float(contrib) if '.' in contrib else int(contrib)
                except ValueError:
                    pass
                    
                assessments.append({
                    "graded_assessment": int(g_id) if g_id.isdigit() else g_id,
                    "type": a_type,
                    "contribution_percent": contrib
                })
                
            for sub in subjects:
                raw_vcaa_records.append({
                    "vcaa_code": sub["code"],
                    "vcaa_subject": sub["name"],
                    "assessments": assessments
                })

    cwd = Path('.')
    conglomerate_path = cwd / "scaling_conglomerate.json"
    conglomerate_data = {}
    
    if conglomerate_path.exists():
        print(f"Loading {conglomerate_path.name} for identifier alignment...")
        try:
            with open(conglomerate_path, "r", encoding="utf-8") as f:
                conglomerate_data = json.load(f)
        except Exception as e:
            print(f"Could not unpack tracking conglomerate data: {e}")
    else:
        print("scaling_conglomerate.json not found. Generating via fallback keys...")

    vcaa_summary = {}
    matched_count = 0
    
    for record in raw_vcaa_records:
        target_key = find_best_vtac_key(record["vcaa_code"], record["vcaa_subject"], conglomerate_data)
        
        if target_key in vcaa_summary and vcaa_summary[target_key]["vcaa_code"] != record["vcaa_code"]:
            target_key = record["vcaa_code"]
            
        if conglomerate_data and target_key in conglomerate_data:
            matched_count += 1
            
        vcaa_summary[target_key] = {
            "vcaa_code": record["vcaa_code"],
            "vcaa_subject": record["vcaa_subject"],
            "assessments": record["assessments"]
        }

    if conglomerate_data:
        print(f"✓ Aligned {matched_count}/{len(raw_vcaa_records)} data matrices to matched VTAC keys.")

    output_path = cwd / "vcaa_summary.json"
    try:
        with open(output_path, "w", encoding="utf-8") as f:
            json.dump(vcaa_summary, f, indent=4, ensure_ascii=False)
        print(f"✓ Successfully generated compiled mapping: {output_path.name}")
    except Exception as e:
        print(f"✗ Failed saving execution vector: {e}")

if __name__ == "__main__":
    main()
