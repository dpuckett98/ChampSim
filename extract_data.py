import pandas as pd

def process_file(filename):
    data = []
    with open(filename, 'r') as f:
        curr_interval = 0
        curr_data = {}
        found_end = False
        for line in f:
            split = line.split(" ")
            if split[0] == "##END##": # only append the last set of data if we find an end
                found_end = True
                data.append(curr_data)
                break
            if split[0] != "interval" or len(split) != 4:
                continue
            if int(split[1]) != curr_interval:
                data.append(curr_data)
                curr_data = {}
                curr_interval = int(split[1])
            curr_data[split[2]] = float(split[3])
        
    return pd.DataFrame(data)

if __name__ == "__main__":
    df = process_file("test.txt")
    print(df)
    print(df.iloc[0])
    print(df.iloc[1])