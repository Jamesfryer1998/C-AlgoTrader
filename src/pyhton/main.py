from data_download import DataDownload

if __name__ == "__main__":
    downloader = DataDownload()
    df = downloader.save_csv()