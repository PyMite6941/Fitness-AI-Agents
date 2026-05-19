if ! python --version &> /dev/null
then
    echo "Python is not installed. Please install Python to continue."
    exit
fi
if ! pip --version &> /dev/null
then
    echo "pip is not installed. Please install pip to continue."
    exit
fi
python -m venv .venv
OS_TYPE=$(uname)
case "$OS_TYPE" in
    "Linux"|"Darwin"*)
        source .venv/bin/activate
        ;;
    "MINGW"*|"CYGWIN"*|"MSYS"*)
        .venv\Scripts\activate
        ;;
    *)
        echo "Unsupported OS: $OS_TYPE"
        exit 1
        ;;
esac
pip install -r requirements.txt