# conglomerate.py
from pathlib import Path
import json
import re

CANONICAL_NAMES = {
    "further mathematics": "General Mathematics",
    "general mathematics": "General Mathematics",
    "specialist mathematics": "Specialist Mathematics"
}

MATH_CODES = {
    # General / Further Maths
    "NF", "NH", "MA10", "FT", "MA071", 
    # Specialist Maths
    "NS", "NH", "MA073", "SP"
}

def build_conglomerate():
    cwd = Path('.')
    json_files = list(cwd.glob("scaling_*.json"))
    json_files = [f for f in json_files if "conglomerate" not in f.name]

    if not json_files:
        print("No scaling_20XX.json files found to conglomerate.")
        return

    print(f"Found {len(json_files)} yearly reports. Compiling master dataset...")
    
    master_data = {}
    years_processed = set()

    for json_path in sorted(json_files):
        year_match = re.search(r'scaling_(\d{4})\.json$', json_path.name)
        if not year_match:
            continue
        year = year_match.group(1)

        try:
            with open(json_path, "r", encoding="utf-8") as f:
                yearly_data = json.load(f)
            
            if yearly_data:
                years_processed.add(year)
                
            for code, metrics in yearly_data.items():
                raw_name = metrics["subject"].strip()
                norm_name = CANONICAL_NAMES.get(raw_name.lower(), raw_name)
                
                if code not in master_data:
                    master_data[code] = {
                        "subject": norm_name
                    }

                master_data[code][year] = {
                    "mean": metrics["mean"],
                    "std_dev": metrics["std_dev"],
                    "scaled_benchmarks": metrics["scaled_benchmarks"]
                }
                
        except Exception as e:
            print(f"✗ Failed incorporating data from {json_path.name}: {e}")

    if not years_processed:
        print("✗ No valid yearly data was processed. Aborting filter.")
        return

    latest_year = max(years_processed)
    print(f"Identified '{latest_year}' as the most recent academic year.")

    initial_count = len(master_data)
    
    master_data = {
        code: data for code, data in master_data.items()
        if (
            latest_year in data 
            or code in MATH_CODES 
            or "General Mathematics" in data["subject"] 
            or "Specialist Mathematics" in data["subject"]
        )
    }
    
    removed_count = initial_count - len(master_data)
    print(f"Filtered out {removed_count} historical subjects inactive in {latest_year}.")

    output_path = cwd / "scaling_conglomerate.json"
    try:
        with open(output_path, "w", encoding="utf-8") as f:
            json.dump(master_data, f, indent=4, ensure_ascii=False)
        print(f"✓ Successfully created master report: {output_path.name}")
    except Exception as e:
        print(f"✗ Failed to save conglomerate JSON: {e}")

if __name__ == "__main__":
    build_conglomerate()