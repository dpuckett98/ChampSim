import pandas as pd
import os

def process_file(filename, bmk, smp, binary):
    data = []
    with open(filename, 'r') as f:
        curr_interval = 0
        curr_data = {"benchmark":bmk, "simpoint":smp, "binary":binary}
        found_end = False
        for line in f:
            split = line.split(" ")
            if split[0] == "##END##": # only append the last set of data if we find an end
                found_end = True
                data.append(curr_data)
                break
            split = split[1:]
            if len(split) != 4 or split[0] != "interval":
                continue
            if int(split[1]) != curr_interval:
                data.append(curr_data)
                curr_data = {"benchmark":bmk, "simpoint":smp, "binary":binary}
                curr_interval = int(split[1])
            curr_data[split[2]] = float(split[3])
        
    return data #pd.DataFrame(data)

def process_dir(path):
    all_data = []
    for f in os.listdir(path):
        bmk, smp, _, binary = f.split("_")
        smp = smp[:-17]
        #binary = binary[12:]
        print("Processing file", f, "bmk", bmk, "smp", smp, "binary", binary)
        new_data = process_file(path + f, bmk, smp, binary)
        all_data = all_data + new_data
    return pd.DataFrame(all_data)

def create_and_save_dataframe(log_path, save_loc):
    df = process_dir(log_path)
    df.to_csv(save_loc)

def build_cpis(base_filename, perfect_filename):
    base_df = pd.read_csv(base_filename)
    perfect_df = pd.read_csv(perfect_filename)
    cpi_df = None
    bin_list = perfect_df["binary"].unique()
    print("Bin list:", bin_list)
    for b in bin_list:
        matching_df = perfect_df.loc[perfect_df["binary"] == b]
        print("Matching DF:", matching_df)
        temp_df = pd.merge(matching_df, base_df, on=["interval", "benchmark", "simpoint"], validate="1:1")
        print(temp_df)
        temp_df[b] = temp_df["cycles_x"] - temp_df["cycles_y"] # perfect - base; so delta is decrease in cycles when bottleneck removed
        print(temp_df)
        new_df = temp_df[["interval", "benchmark", "simpoint", b]]
        if cpi_df is None:
            cpi_df = new_df
        else:
            cpi_df = pd.merge(cpi_df, new_df, on=["interval", "benchmark", "simpoint"], validate="m:1")
    print(cpi_df)
    cpi_df.to_csv("bottlenecks_instr_spec_2017.csv", index=False)

if __name__ == "__main__":
    build_cpis("perf_ctrs_instr_spec_2017.csv", "perfect_instr_spec_2017.csv")
    #create_and_save_dataframe("/mnt/research/Gratz_Paul_V/Students/Puckett_Daniel/ChampSim_event_listeners/perfect_logs_v1/2025-03-13/44_cores/1/", "test_dataframe.csv")
    #create_and_save_dataframe("/mnt/research/Gratz_Paul_V/Students/Puckett_Daniel/ChampSim_event_listeners/perfect_logs_v1/2025-03-19/44_cores/4/", "bottlenecks_instr_spec_2017.csv")
    
    #create_and_save_dataframe("/mnt/research/Gratz_Paul_V/Students/Puckett_Daniel/ChampSim_event_listeners/perfect_logs_v1/perf_ctrs/44_cores/1/", "perf_ctrs_instr_spec_2017.csv")
    #create_and_save_dataframe("/mnt/research/Gratz_Paul_V/Students/Puckett_Daniel/ChampSim_event_listeners/perfect_logs_v1/2025-03-17/44_cores/1/", "test_dataframe.csv")
    #df = process_file("test.txt")
    #print(df)
    #print(df.iloc[0])
    #print(df.iloc[1])
    #df = process_file("results_1.txt")
    #print(df)
    #print(df["d_cache_comp"])
    #print(df.iloc[0])
    #print(df.iloc[1])
