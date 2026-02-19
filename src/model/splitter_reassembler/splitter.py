import os

def split_file(filepath, chunk_size=1024 * 1024):  
    chunks = []
    file_size = os.path.getsize(filepath)

    with open(filepath, "rb") as f:
        chunk_index = 0
        while True:
            data = f.read(chunk_size)
            if not data:
                break

            chunks.append({
                "index": chunk_index,
                "data": data
            })
            chunk_index += 1

    return {
        "filename": os.path.basename(filepath),
        "filesize": file_size,
        "total_chunks": len(chunks),
        "chunks": chunks
    }


def reassemble_file(file_info, output_path):
    chunks = sorted(file_info["chunks"], key=lambda x: x["index"])

    with open(output_path, "wb") as f:
        for chunk in chunks:
            f.write(chunk["data"])
