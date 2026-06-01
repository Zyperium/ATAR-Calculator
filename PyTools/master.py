from pathlib import Path
import dl
import process
import conglomerate
import vcaa_summary

def cleanup():
    print("Cleaning up raw PDF files...")
    cwd = Path('.')
    for pdf_file in cwd.glob("scaling_*.pdf"):
        try:
            pdf_file.unlink()
            print(f"Removed temporary artifact: {pdf_file.name}")
        except OSError as e:
            print(f"Could not remove {pdf_file.name}: {e}")

    for json_file in cwd.glob("scaling_*.json"):
        if json_file.name == "scaling_conglomerate.json":
            continue
        try:
            json_file.unlink()
            print(f"Removed temporary artifact: {json_file.name}")
        except OSError as e:
            print(f"Could not remove {json_file.name}: {e}")

def main():
    dl.main()
    print("-" * 30)
    
    process.main()
    print("-" * 30)
    
    conglomerate.build_conglomerate()
    print("-" * 30)
    
    vcaa_summary.main()
    print("-" * 30)
    
    cleanup()
    print("\nWorkflow complete! Master JSON vector arrays ready for C++ ingestion.")

if __name__ == "__main__":
    main()