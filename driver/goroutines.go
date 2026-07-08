package main

import (
	"fmt"
	"os/exec"
	"sync"
	"time"
)

// This is the "around" part.
// C (amosoz.c) is the single-file OS center with real scheduler.
// Go goroutines are users / utilities / concurrent drivers around the C kernel.

func osUser(id int, wg *sync.WaitGroup) {
	defer wg.Done()

	// Each goroutine acts as a "user process" or utility.
	// It launches its own instance of the C OS binary and drives it.
	// In a more advanced setup these could share one OS instance via sockets/pipes.

	cmd := exec.Command("./amosoz")
	stdin, _ := cmd.StdinPipe()
	// We feed a short script of commands to exercise the scheduler inside C.
	script := fmt.Sprintf("run goroutine-user-%d\nps\ntick\nps\nkill 3\ntick\nexit\n", id)
	go func() {
		stdin.Write([]byte(script))
		stdin.Close()
	}()

	out, err := cmd.CombinedOutput()
	if err != nil {
		fmt.Printf("user %d error: %v\n", id, err)
		return
	}
	fmt.Printf("=== goroutine user %d output (drove C OS) ===\n%s\n", id, string(out))
}

func main() {
	fmt.Println("C is center (single file amosoz.c with scheduler).")
	fmt.Println("Go goroutines around it simulating concurrent users/utilities.")

	var wg sync.WaitGroup
	for i := 0; i < 3; i++ {
		wg.Add(1)
		go osUser(i, &wg)
		time.Sleep(10 * time.Millisecond) // slight stagger
	}
	wg.Wait()
	fmt.Println("All goroutine users finished. C scheduler handled the foundation.")
}
