package main

import (
    "encoding/binary"
    "fmt"
    "io"
    "io/ioutil"
    "math/rand"
    "net"
    "os"
    "os/user"
    "path"
    "time"

    log "github.com/sirupsen/logrus"
    "github.com/spf13/cobra"
    yaml "gopkg.in/yaml.v2"
)

type connectorArgs struct {
    configFile string
    logLevel   string
}

var params connectorArgs

func init() {
    rootCmd.PersistentFlags().StringVar(&params.configFile,
        "config", "", "config file (default is $HOME/.frozy-envoy-connector.yaml)")
    rootCmd.PersistentFlags().StringVar(&params.logLevel,
        "log-level", "info", "logging level (default is info)")
    log.SetFormatter(&log.TextFormatter{})
    log.SetOutput(os.Stdout)
}

var rootCmd = &cobra.Command{
    Use:   "connector",
    Short: "Envoy Upstream Connector",
    Run: func(cmd *cobra.Command, args []string) {
        if lvl, err := log.ParseLevel(params.logLevel); err != nil {
            log.SetLevel(log.InfoLevel)
            log.Error(err)
        } else {
            log.SetLevel(lvl)
        }

        rand.Seed(time.Now().UTC().UnixNano())
        config := Config{}
        if err := config.Load(params.configFile); err != nil {
            os.Exit(1)
        }

        log.Debugf("Initializing with:\n%s", config.String())

        for {
            run(config)
            time.Sleep(time.Second * 2)
        }
    },
}

func main() {
    if err := rootCmd.Execute(); err != nil {
        log.Error("Fatal error:", err.Error())
        os.Exit(1)
    }
}

func run(config Config) {
    if envoy, err := net.DialTimeout("tcp", config.Envoy.String(), ConnectTimeout); err != nil {
        log.Error("Dial into Envoy-control error:", err.Error())
    } else {
        if _, err := envoy.Write(make([]byte, 8)); err != nil {
            log.Error("Failed to send control initialization message.", err)
            envoy.Close()
            return
        }
        log.Infof("ENVOY at %s <-- CONNECTOR --> SERVICE at %s",
            config.Envoy.String(), config.Service.String())
        for {
            buf := make([]byte, 8)
            re, err := envoy.Read(buf)
            if err != nil || re != 8 {
                log.Errorf("Failed to read data from envoy. Error: %s, Bytes read: %d", err.Error(), re)
                envoy.Close()
                break
            }

            log.Trace("Received connection request C", binary.LittleEndian.Uint64(buf))
            if target, err := net.DialTimeout("tcp", config.Service.String(), ConnectTimeout); err != nil {
                log.Error("Dial into target service error.", err.Error())
            } else {
                if conn, err := net.DialTimeout("tcp", config.Envoy.String(), ConnectTimeout); err != nil {
                    log.Error("Dial into Envoy-service error:", err.Error())
                    target.Close()
                } else {
                    if _, err := conn.Write(buf); err != nil {
                        log.Error("Failed to send connection id.", err)
                        conn.Close()
                        target.Close()
                    } else {
                        log.Debugf("Run tunneling for C%d", binary.LittleEndian.Uint64(buf))
                        go connectionForward(conn, target)
                    }
                }
            }
        }
    }
}

func connectionForward(conn net.Conn, target net.Conn) {
    doneR2T := make(chan error)
    doneT2R := make(chan error)

    go func() {
        _, err := io.Copy(conn, target)
        doneT2R <- err
    }()

    go func() {
        _, err := io.Copy(target, conn)
        doneR2T <- err
    }()

    var fin error
    select {
    case <-doneR2T:
        target.Close()
        conn.Close()
        fin = <-doneT2R
    case <-doneT2R:
        target.Close()
        conn.Close()
        fin = <-doneR2T
    }
    log.Trace("Closed:", fin)
}

// Configuration

// ConnectTimeout default
const ConnectTimeout = time.Second * 5

// Endpoint is just a pair of string and uint16
type Endpoint struct {
    Host string `yaml:",omitempty"`
    Port uint16 `yaml:",omitempty"`
}

// Network net.Addr interface
func (addr Endpoint) Network() string { return "tcp" }
func (addr Endpoint) String() string  { return fmt.Sprintf("%s:%d", addr.Host, addr.Port) }

// IsEmpty .
func (addr Endpoint) IsEmpty() bool { return addr.Host == "" || addr.Port == 0 }

// Config is for application initialization
type Config struct {
    Envoy   Endpoint
    Service Endpoint
}

// Load configuration
func (c *Config) Load(optionalConfig string) error {
    loadPath := configDefaultPath()
    if optionalConfig != "" {
        loadPath = optionalConfig
    }

    log.Debug("Loading config from ", loadPath)
    var (
        bs  []byte
        err error
    )

    if bs, err = ioutil.ReadFile(loadPath); err != nil {
        log.Error("Failed to read configuration file: ", err)
        return err
    }

    if err = yaml.Unmarshal(bs, c); err != nil {
        log.Error("Failed to parse configuration file: ", err)
        return err
    }

    return nil
}

func (c Config) String() string {
    bs, _ := yaml.Marshal(c)
    return string(bs)
}

func currentUserHomeDir() string {
    user, err := user.Current()
    if err == nil {
        return user.HomeDir
    }
    return os.Getenv("HOME")
}

func configDefaultPath() string {
    return path.Join(currentUserHomeDir(), ".frozy-envoy-connector.yaml")
}
