from pathlib import Path
import json
import re
import pypdf

def extract_atar_table(pdf_path, output_json_path="atar_from_aggregate.json"):
    """
    Robustly extracts the complete Scaled Aggregate to ATAR table by grouping
    all numeric sequences found within the target table page zone.
    """
    reader = pypdf.PdfReader(pdf_path)
    atar_map = {}
    
    table_started = False
    all_numbers = []
    
    for page in reader.pages:
        text = page.extract_text()
        if not text:
            continue
            
        if "scaled aggregate" in text.lower() and "atar" in text.lower():
            table_started = True
            
        if table_started:
            cleaned_text = text.replace('\xa0', ' ')
            
            page_numbers = re.findall(r'\d+\.\d+|\d+', cleaned_text)
            all_numbers.extend(page_numbers)
            
            if "notional" in text.lower() and len(all_numbers) > 50:
                break

    i = 0
    while i < len(all_numbers) - 1:
        val1 = float(all_numbers[i])
        val2 = float(all_numbers[i+1])
        
        if 0.0 <= val1 <= 220.0 and 0.0 <= val2 <= 99.95 and val1 > val2:
            atar_map[str(val1)] = val2
            i += 2
        else:
            i += 1

    if atar_map:
        sorted_atar_map = {k: atar_map[k] for k in sorted(atar_map.keys(), key=float, reverse=True)}
        
        with open(output_json_path, "w", encoding="utf-8") as f:
            json.dump(sorted_atar_map, f, indent=4, ensure_ascii=False)
        print(f"✓ Successfully generated COMPLETE ATAR mapping table ({len(sorted_atar_map)} steps): {output_json_path}")
        return True
        
    print("Could not locate or parse the Scaled Aggregate to ATAR table layout.")
    return False

def parse_pdf(pdf_path):
    data = {}
    reader = pypdf.PdfReader(pdf_path)
    
    pattern_str = r'(?:^|\b)([A-Z0-9]{2,6})\s+(.+?)\s+([\d\s\.]+)'
    row_pattern = re.compile(pattern_str, re.MULTILINE)

    omit_phrases = ["small study", "no candidates", "see note", "all small lotes", "study mean st. dev"]

    for page in reader.pages:
        text = page.extract_text()
        if not text: 
            continue
            
        cleaned_text = text.replace('\xa0', ' ')
        lines = [line.strip() for line in cleaned_text.split('\n') if line.strip()]
        
        for line in lines:
            match = row_pattern.search(line)
            if match:
                code = match.group(1).strip()
                
                if code.isdigit() and len(code) == 4:
                    continue
                    
                subject_name = match.group(2).strip()
                subject_name = re.sub(r'\s*\(.*?\)', '', subject_name).strip()
                
                if any(phrase in subject_name.lower() for phrase in omit_phrases):
                    continue
                
                numbers = re.findall(r'\d+\.\d+|\d+', match.group(3))
                
                if len(numbers) < 5:
                    continue
                    
                try:
                    mean = float(numbers[0])
                    std_dev = float(numbers[1])
                    benchmarks = [int(x) for x in numbers[2:]]
                    
                    data[code] = {
                        "subject": subject_name,
                        "mean": mean,
                        "std_dev": std_dev,
                        "scaled_benchmarks": benchmarks
                    }
                except ValueError:
                    continue
    return data

def main():
    cwd = Path('.')
    pdf_files = list(cwd.glob("scaling_*.pdf"))
    
    if not pdf_files:
        print("No local scaling_*.pdf files found to process.")
        return

    print("Processing PDFs to JSON...")
    for pdf_path in pdf_files:
        year_match = re.search(r'scaling_(\d{4})\.pdf$', pdf_path.name)
        if not year_match: 
            continue
        
        year = year_match.group(1)
        
        if year == "2025":
            print("Targeting 2025 core report for ATAR Matrix profile...")
            extract_atar_table(pdf_path)
            
        json_path = cwd / f"scaling_{year}.json"
        try:
            parsed_data = parse_pdf(pdf_path)
            with open(json_path, "w", encoding="utf-8") as f:
                json.dump(parsed_data, f, indent=4, ensure_ascii=False)
            print(f"✓ Processed: {json_path.name}")
        except Exception as e:
            print(f"✗ Failed processing {pdf_path.name}: {e}")

if __name__ == "__main__":
    main()